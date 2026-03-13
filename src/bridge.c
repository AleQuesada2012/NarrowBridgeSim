#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bridge.h"

/* ============================= */
/* ===== Heap Helpers      ===== */
/* ============================= */

/*
 * Min-heap ordered by (priority ASC, seq ASC).
 * Lower priority value = higher urgency (0 = ambulance).
 * Equal priority is broken by arrival sequence (FIFO within same class).
 */

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

    /* Sift up */
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap_before(h->data[i], h->data[parent])) {
            WaitNode *tmp    = h->data[i];
            h->data[i]       = h->data[parent];
            h->data[parent]  = tmp;
            i = parent;
        } else {
            break;
        }
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

    /* Sift down */
    int i = 0;
    for (;;) {
        int left  = 2 * i + 1;
        int right = 2 * i + 2;
        int best  = i;

        if (left  < h->size && heap_before(h->data[left],  h->data[best])) best = left;
        if (right < h->size && heap_before(h->data[right], h->data[best])) best = right;

        if (best == i) break;

        WaitNode *tmp   = h->data[i];
        h->data[i]      = h->data[best];
        h->data[best]   = tmp;
        i = best;
    }
}

/* ============================= */
/* ===== Wake Head of Queue ==== */
/* ============================= */

/*
 * Marks the head node as ready and signals its personal cond var.
 * Must be called with b->lock held.
 */
static void wake_head(WaitHeap *h)
{
    WaitNode *head = heap_peek(h);
    if (!head) return;
    head->ready = 1;
    pthread_cond_signal(&head->cv);
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
    return b;
}

void bridge_destroy(Bridge *b)
{
    for (int i = 0; i < b->length; i++)
        pthread_mutex_destroy(&b->slots[i]);

    free(b->slots);
    pthread_mutex_destroy(&b->lock);

    printf("=== Bridge destroyed. ===\n");
    free(b);
}

/* ============================= */
/* ===== Synchronization  ====== */
/* ============================= */

void bridge_enter(Bridge *b, BridgeVehicleInfo *info)
{
    /* ----------------------------------------------------------
     * Build a wait node on this thread's stack.
     * The cond var lives here; no heap allocation needed.
     * ---------------------------------------------------------- */
    WaitNode node;
    pthread_cond_init(&node.cv, NULL);
    node.ready = 0;

    pthread_mutex_lock(&b->lock);

    /* Assign sequence number and priority atomically under the lock */
    node.seq = b->next_seq++;

    /*
     * Priority assignment:
     *   0  — ambulance (highest urgency, jumps the queue)
     *   1+ — normal vehicles (FIFO within their class)
     *
     * Using seq directly as normal priority keeps FIFO ordering for
     * regular cars without any extra counter.
     */
    node.priority = info->is_ambulance ? 0 : (int)node.seq;

    printf("[VEHICLE %d] Arrived at bridge from %s%s\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "");

    /* Update arrival counters */
    if (info->direction == EAST) {
        b->waiting_east++;
        if (info->is_ambulance) b->ambulances_waiting_east++;
    } else {
        b->waiting_west++;
        if (info->is_ambulance) b->ambulances_waiting_west++;
    }

    /* Insert into the appropriate priority queue */
    WaitHeap *my_queue  = (info->direction == EAST) ? &b->east_queue : &b->west_queue;
    heap_push(my_queue, &node);

    /*
     * If the bridge is currently free (or already running our direction)
     * and no opposing ambulance is blocking us, and we are now the head,
     * signal ourselves immediately so we don't wait unnecessarily.
     */
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

    /* ----------------------------------------------------------
     * Wait condition:
     *   (a) The bridge is occupied in the opposite direction, OR
     *   (b) We are a normal car and an ambulance is waiting on the
     *       opposite side while the bridge currently favours us
     *       (we must yield so the bridge can clear and flip).
     *   (c) We are not at the head of our own queue yet (another
     *       vehicle of equal-or-higher priority is ahead of us).
     * ---------------------------------------------------------- */
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

        pthread_cond_wait(&node.cv, &b->lock);
    }

    /* We are the head — pop ourselves from the queue */
    heap_pop(my_queue);

    /* Update counters now that we are actually entering */
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

    /*
     * If there is a next vehicle in our queue that can also enter
     * (same direction, no opposing ambulance blocking), wake it now.
     * This allows multiple same-direction vehicles to pipeline onto
     * the bridge without waiting for each one to fully cross first.
     */
    {
        int opp_amb = (info->direction == EAST)
                      ? b->ambulances_waiting_west
                      : b->ambulances_waiting_east;

        if (opp_amb == 0 && heap_peek(my_queue) != NULL) {
            wake_head(my_queue);
        }
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

    if (b->cars_on_bridge == 0)
    {
        /* -------------------------------------------------------
         * Priority order for deciding which side goes next:
         *   1. Ambulance waiting EAST
         *   2. Ambulance waiting WEST
         *   3. Normal flow: alternate if opposite side has waiters
         *   4. Same-side still has waiters (keep direction)
         * ----------------------------------------------------- */

        if (b->ambulances_waiting_east > 0) {
            b->current_direction = EAST;
            printf("[BRIDGE] PRIORITY: ambulance waiting EAST\n");
            wake_head(&b->east_queue);

        } else if (b->ambulances_waiting_west > 0) {
            b->current_direction = WEST;
            printf("[BRIDGE] PRIORITY: ambulance waiting WEST\n");
            wake_head(&b->west_queue);

        } else if (b->current_direction == EAST) {
            if (b->waiting_west > 0) {
                b->current_direction = WEST;
                printf("[BRIDGE] Switching direction to WEST\n");
                wake_head(&b->west_queue);
            } else {
                wake_head(&b->east_queue);
            }
        } else {
            if (b->waiting_east > 0) {
                b->current_direction = EAST;
                printf("[BRIDGE] Switching direction to EAST\n");
                wake_head(&b->east_queue);
            } else {
                wake_head(&b->west_queue);
            }
        }
    }

    pthread_mutex_unlock(&b->lock);
}

/* ============================= */
/* ===== Monitoring Stubs  ===== */
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

    /* Wake the head of the newly favoured side */
    WaitHeap *q = (dir == EAST) ? &bridge->east_queue : &bridge->west_queue;
    wake_head(q);

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

/* Passed-count stubs — implement when stats tracking is added */
int bridge_get_passed_count(Bridge *bridge, Direction dir)
{
    (void)bridge; (void)dir;
    return 0;
}

void bridge_reset_passed_count(Bridge *bridge, Direction dir)
{
    (void)bridge; (void)dir;
}