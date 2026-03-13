#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bridge.h"

/* ============================= */
/* ===== Heap Helpers      ===== */
/* ============================= */

static inline int heap_before(const WaitNode *a, const WaitNode *b)
{
    if (a->priority != b->priority)
        return a->priority < b->priority;
    return a->seq < b->seq;
}

static void heap_push(WaitHeap *h, WaitNode *node)
{
    int i = h->size++;
    h->data[i] = node;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap_before(h->data[i], h->data[parent])) {
            WaitNode *tmp   = h->data[i];
            h->data[i]      = h->data[parent];
            h->data[parent] = tmp;
            i = parent;
        } else break;
    }
}

static WaitNode *heap_peek(const WaitHeap *h)
{
    return (h->size > 0) ? h->data[0] : NULL;
}

static void heap_pop(WaitHeap *h)
{
    if (h->size == 0) return;
    h->data[0] = h->data[--h->size];
    int i = 0;
    for (;;) {
        int left  = 2 * i + 1;
        int right = 2 * i + 2;
        int best  = i;
        if (left  < h->size && heap_before(h->data[left],  h->data[best])) best = left;
        if (right < h->size && heap_before(h->data[right], h->data[best])) best = right;
        if (best == i) break;
        WaitNode *tmp  = h->data[i];
        h->data[i]     = h->data[best];
        h->data[best]  = tmp;
        i = best;
    }
}

/* ============================= */
/* ===== Wake Head of Queue ==== */
/* ============================= */

static void wake_head(WaitHeap *h)
{
    WaitNode *head = heap_peek(h);
    if (!head) return;
    head->ready = 1;
    pthread_cond_signal(&head->cv);
}

/* ============================= */
/* ===== Queue State Log   ===== */
/* ============================= */

/*
 * Emits a structured queue-state line consumed by the GUI.
 *
 * Format: [QUEUE <direction> <total_waiting> <ambulances_waiting>]
 *   direction        — "EAST" or "WEST"
 *   total_waiting    — vehicles currently queued on that side
 *   ambulances_waiting — subset that are ambulances
 *
 * Must be called with b->lock held.
 */
static void emit_queue_state(const Bridge *b, Direction dir)
{
    if (dir == EAST)
        printf("[QUEUE EAST %d %d]\n",
               b->waiting_east, b->ambulances_waiting_east);
    else
        printf("[QUEUE WEST %d %d]\n",
               b->waiting_west, b->ambulances_waiting_west);
    fflush(stdout);
}

/*
 * Emits the current bridge direction.
 * Format: [DIRECTION <EAST|WEST|NONE>]
 */
static void emit_direction(const Bridge *b)
{
    const char *d = (b->current_direction == EAST) ? "EAST"
                  : (b->current_direction == WEST) ? "WEST"
                  :                                  "NONE";
    printf("[DIRECTION %s]\n", d);
    fflush(stdout);
}

/* ============================= */
/* ===== Bridge Lifecycle  ===== */
/* ============================= */

Bridge *bridge_create(const Config *config)
{
    Bridge *b = malloc(sizeof(Bridge));

    b->length = config->bridge_length;

    b->slots = malloc(sizeof(pthread_mutex_t) * b->length);
    for (int i = 0; i < b->length; i++)
        pthread_mutex_init(&b->slots[i], NULL);

    pthread_mutex_init(&b->lock, NULL);

    b->cars_on_bridge          = 0;
    b->waiting_east            = 0;
    b->waiting_west            = 0;
    b->ambulances_waiting_east = 0;
    b->ambulances_waiting_west = 0;
    b->current_direction       = EAST;
    b->next_seq                = 1;

    b->east_queue.size = 0;
    b->west_queue.size = 0;

    printf("Bridge created with length: %d meters.\n", b->length);

    /* Let the GUI know the initial direction */
    emit_direction(b);

    return b;
}

void bridge_destroy(Bridge *b)
{
    for (int i = 0; i < b->length; i++)
        pthread_mutex_destroy(&b->slots[i]);

    free(b->slots);
    pthread_mutex_destroy(&b->lock);

    printf("=== Bridge destroyed. ===\n");
    fflush(stdout);
    free(b);
}

/* ============================= */
/* ===== Synchronization  ====== */
/* ============================= */

void bridge_enter(Bridge *b, BridgeVehicleInfo *info)
{
    WaitNode node;
    pthread_cond_init(&node.cv, NULL);
    node.ready = 0;

    pthread_mutex_lock(&b->lock);

    node.seq      = b->next_seq++;
    node.priority = info->is_ambulance ? 0 : (int)node.seq;

    printf("[VEHICLE %d] Arrived at bridge from %s%s\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "");
    fflush(stdout);

    /* Update arrival counters */
    if (info->direction == EAST) {
        b->waiting_east++;
        if (info->is_ambulance) b->ambulances_waiting_east++;
    } else {
        b->waiting_west++;
        if (info->is_ambulance) b->ambulances_waiting_west++;
    }

    /* Emit queue state so GUI reflects the new arrival */
    emit_queue_state(b, info->direction);

    WaitHeap *my_queue = (info->direction == EAST) ? &b->east_queue : &b->west_queue;
    heap_push(my_queue, &node);

    /* Self-signal if we can enter immediately */
    {
        int opp_amb = (info->direction == EAST)
                      ? b->ambulances_waiting_west
                      : b->ambulances_waiting_east;

        int can_enter =
            (b->cars_on_bridge == 0 || b->current_direction == info->direction) &&
            (info->is_ambulance || opp_amb == 0);

        if (can_enter && heap_peek(my_queue) == &node)
            wake_head(my_queue);
    }

    /* Wait loop */
    for (;;) {
        int opp_amb = (info->direction == EAST)
                      ? b->ambulances_waiting_west
                      : b->ambulances_waiting_east;

        int blocked_by_direction =
            (b->cars_on_bridge > 0 && b->current_direction != info->direction);

        int blocked_by_opp_amb =
            (!info->is_ambulance &&
             opp_amb > 0 &&
             b->current_direction == info->direction);

        int not_my_turn = (heap_peek(my_queue) != &node) || !node.ready;

        if (!blocked_by_direction && !blocked_by_opp_amb && !not_my_turn)
            break;

        printf("[VEHICLE %d] Waiting (queue pos %d, priority %d)%s\n",
               info->id,
               my_queue->size,
               node.priority,
               info->is_ambulance ? " [AMBULANCE]" : "");
        fflush(stdout);

        pthread_cond_wait(&node.cv, &b->lock);
    }

    /* Pop ourselves and decrement counters */
    heap_pop(my_queue);

    if (info->direction == EAST) {
        b->waiting_east--;
        if (info->is_ambulance) b->ambulances_waiting_east--;
    } else {
        b->waiting_west--;
        if (info->is_ambulance) b->ambulances_waiting_west--;
    }

    b->cars_on_bridge++;
    b->current_direction = info->direction;

    printf("[BRIDGE] Vehicle %d entered from %s (priority %d). Cars on bridge: %d\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           node.priority,
           b->cars_on_bridge);
    fflush(stdout);

    /* Emit updated queue state and direction */
    emit_queue_state(b, info->direction);
    emit_direction(b);

    /* Wake the next vehicle in our queue if safe to do so */
    {
        int opp_amb = (info->direction == EAST)
                      ? b->ambulances_waiting_west
                      : b->ambulances_waiting_east;

        if (opp_amb == 0 && heap_peek(my_queue) != NULL)
            wake_head(my_queue);
    }

    pthread_mutex_unlock(&b->lock);

    pthread_mutex_lock(&b->slots[0]);

    pthread_cond_destroy(&node.cv);
}

void bridge_advance(Bridge *b, int position)
{
    pthread_mutex_lock(&b->slots[position + 1]);
    pthread_mutex_unlock(&b->slots[position]);
}

void bridge_leave(Bridge *b, BridgeVehicleInfo *info)
{
    pthread_mutex_unlock(&b->slots[b->length - 1]);

    pthread_mutex_lock(&b->lock);

    b->cars_on_bridge--;

    printf("[BRIDGE] Vehicle %d exited going %s. Cars remaining: %d\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           b->cars_on_bridge);
    fflush(stdout);

    if (b->cars_on_bridge == 0)
    {
        if (b->ambulances_waiting_east > 0) {
            b->current_direction = EAST;
            printf("[BRIDGE] PRIORITY: ambulance waiting EAST\n");
            fflush(stdout);
            wake_head(&b->east_queue);

        } else if (b->ambulances_waiting_west > 0) {
            b->current_direction = WEST;
            printf("[BRIDGE] PRIORITY: ambulance waiting WEST\n");
            fflush(stdout);
            wake_head(&b->west_queue);

        } else if (b->current_direction == EAST) {
            if (b->waiting_west > 0) {
                b->current_direction = WEST;
                printf("[BRIDGE] Switching direction to WEST\n");
                fflush(stdout);
                wake_head(&b->west_queue);
            } else {
                wake_head(&b->east_queue);
            }
        } else {
            if (b->waiting_east > 0) {
                b->current_direction = EAST;
                printf("[BRIDGE] Switching direction to EAST\n");
                fflush(stdout);
                wake_head(&b->east_queue);
            } else {
                wake_head(&b->west_queue);
            }
        }

        emit_direction(b);
    }

    pthread_mutex_unlock(&b->lock);
}

/* ============================= */
/* ===== Monitoring API    ===== */
/* ============================= */

int bridge_get_length(Bridge *bridge)
{
    return bridge ? bridge->length : -1;
}

Direction bridge_get_direction(Bridge *bridge)
{
    return bridge ? bridge->current_direction : NONE;
}

void bridge_set_direction(Bridge *bridge, Direction dir)
{
    if (!bridge) return;
    pthread_mutex_lock(&bridge->lock);
    bridge->current_direction = dir;
    WaitHeap *q = (dir == EAST) ? &bridge->east_queue : &bridge->west_queue;
    wake_head(q);
    emit_direction(bridge);
    pthread_mutex_unlock(&bridge->lock);
}

int bridge_get_waiting(Bridge *bridge, Direction dir)
{
    if (!bridge) return 0;
    return (dir == EAST) ? bridge->waiting_east : bridge->waiting_west;
}

int bridge_get_ambulances_waiting(Bridge *bridge, Direction dir)
{
    if (!bridge) return 0;
    return (dir == EAST) ? bridge->ambulances_waiting_east
                         : bridge->ambulances_waiting_west;
}

int bridge_get_cars_on_bridge(Bridge *bridge)
{
    return bridge ? bridge->cars_on_bridge : 0;
}

int bridge_get_passed_count(Bridge *bridge, Direction dir)
{
    (void)bridge; (void)dir;
    return 0;
}

void bridge_reset_passed_count(Bridge *bridge, Direction dir)
{
    (void)bridge; (void)dir;
}