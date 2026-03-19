#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "bridge.h"

/* ===================================================== */
/* =================== FIFO HELPERS ==================== */
/* ===================================================== */

static void fifo_push(FifoQueue *q, FifoNode *node)
{
    node->next = NULL;
    if (q->tail)
        q->tail->next = node;
    else
        q->head = node;
    q->tail = node;
    q->size++;
}

static void fifo_pop(FifoQueue *q)
{
    if (!q->head) return;
    q->head = q->head->next;
    if (!q->head)
        q->tail = NULL;
    q->size--;
}

/* ===================================================== */
/* ============= STRUCTURED GUI LOG HELPERS ============ */
/* ===================================================== */

/*
 * Emit structured lines consumed by the GUI parser.
 * All must be called with bridge->lock held.
 */
static void emit_queue(const Bridge *b, Direction side)
{
    /*
     * Format: [QUEUE EAST|WEST id0:amb0 id1:amb1 ...]
     *
     * Each token is  <vehicle_id>:<is_ambulance>  in head-to-tail FIFO
     * order, so the GUI renders icons in exact arrival order.
     * An empty queue emits [QUEUE EAST] with no tokens.
     */
    printf("[QUEUE %s", side == EAST ? "EAST" : "WEST");
    for (const FifoNode *n = b->queue[side].head; n; n = n->next)
        printf(" %d:%d", n->id, n->is_ambulance);
    printf("]\n");
    fflush(stdout);
}

static void emit_direction(const Bridge *b)
{
    const char *d =
        b->current_direction == EAST ? "EAST" :
        b->current_direction == WEST ? "WEST" : "NONE";
    printf("[DIRECTION %s]\n", d);
    fflush(stdout);
}

/* ===================================================== */
/* ================ ENTRY PREDICATE ==================== */
/* ===================================================== */

static int can_head_enter(const Bridge *b, Direction side)
{
    const FifoNode *head = b->queue[side].head;
    if (!head) return 0;

    Direction       opp      = (side == EAST) ? WEST : EAST;
    const FifoNode *opp_head = b->queue[opp].head;

    int bridge_clear   = (b->cars_on_bridge == 0);
    int same_direction = (b->current_direction == side);
    int bridge_ok      = bridge_clear || same_direction;

    int must_yield = (!head->is_ambulance &&
                      opp_head != NULL &&
                      opp_head->is_ambulance);

    if (b->mode == MODE_SEMAPHORE) {
        LightState my_light = b->light[side];

        if (head->is_ambulance) {
            return bridge_ok && !must_yield;
        }

        if (my_light == LIGHT_RED)
            return 0;

        return bridge_ok && !must_yield;
    }

    return bridge_ok && !must_yield;
}

static void try_wake_head(Bridge *b, Direction side)
{
    if (b->queue[side].head && can_head_enter(b, side))
        pthread_cond_signal(&b->queue[side].head->cv);
}

/* ===================================================== */
/* ================ BRIDGE LIFECYCLE =================== */
/* ===================================================== */

Bridge *bridge_create(const Config *config)
{
    Bridge *b = malloc(sizeof(Bridge));

    b->length = config->bridge_length;
    b->mode   = config->mode;

    b->slots = malloc(sizeof(pthread_mutex_t) * b->length);
    for (int i = 0; i < b->length; i++)
        pthread_mutex_init(&b->slots[i], NULL);

    pthread_mutex_init(&b->lock, NULL);

    b->cars_on_bridge    = 0;
    b->current_direction = NONE;

    for (int s = 0; s < 2; s++) {
        b->queue[s].head         = NULL;
        b->queue[s].tail         = NULL;
        b->queue[s].size         = 0;
        b->waiting[s]            = 0;
        b->ambulances_waiting[s] = 0;
        b->passed_count[s]       = 0;
        b->light[s]              = LIGHT_OFF;
    }

    printf("[BRIDGE] Created with length: %d meters.\n", b->length);
    fflush(stdout);
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

/* ===================================================== */
/* =============== SYNCHRONIZATION LOGIC =============== */
/* ===================================================== */

void bridge_enter(Bridge *b, BridgeVehicleInfo *info)
{
    Direction  side = info->direction;
    FifoQueue *q    = &b->queue[side];

    FifoNode node;
    pthread_cond_init(&node.cv, NULL);
    node.is_ambulance = info->is_ambulance;
    node.id           = info->id;
    node.next         = NULL;

    pthread_mutex_lock(&b->lock);

    b->waiting[side]++;
    if (info->is_ambulance) b->ambulances_waiting[side]++;

    fifo_push(q, &node);

    printf("[VEHICLE %d] Arrived at bridge from %s%s. Queue: %d waiting\n",
           info->id,
           side == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "",
           q->size);
    fflush(stdout);

    /* Emit updated queue state for the GUI */
    emit_queue(b, side);

    if (info->is_ambulance) {
        Direction opp = (side == EAST) ? WEST : EAST;
        if (b->queue[opp].head)
            pthread_cond_signal(&b->queue[opp].head->cv);
    }

    while (q->head != &node || !can_head_enter(b, side))
        pthread_cond_wait(&node.cv, &b->lock);

    /* Cleared to enter */
    fifo_pop(q);

    b->waiting[side]--;
    if (info->is_ambulance) b->ambulances_waiting[side]--;

    b->cars_on_bridge++;
    b->current_direction = side;
    b->passed_count[side]++;

    printf("[BRIDGE] Vehicle %d entered from %s%s. Cars on bridge: %d\n",
           info->id,
           side == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "",
           b->cars_on_bridge);
    fflush(stdout);

    if (b->mode == MODE_SEMAPHORE &&
        info->is_ambulance        &&
        b->light[side] == LIGHT_RED)
    {
        printf("[SEMAPHORE] Ambulance %d crossing on RED (%s). "
               "Light timer unchanged.\n",
               info->id, side == EAST ? "EAST" : "WEST");
        fflush(stdout);
    }

    /* Emit queue and direction for the GUI */
    emit_queue(b, side);
    emit_direction(b);

    try_wake_head(b, side);

    Direction opp = (side == EAST) ? WEST : EAST;
    try_wake_head(b, opp);

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
    Direction side = info->direction;
    Direction opp  = (side == EAST) ? WEST : EAST;

    pthread_mutex_unlock(&b->slots[b->length - 1]);

    pthread_mutex_lock(&b->lock);

    b->cars_on_bridge--;

    printf("[BRIDGE] Vehicle %d exited going %s. Cars remaining: %d\n",
           info->id,
           side == EAST ? "EAST" : "WEST",
           b->cars_on_bridge);
    fflush(stdout);

    if (b->cars_on_bridge == 0) {
        b->current_direction = NONE;

        int opp_amb  = b->queue[opp].head  && b->queue[opp].head->is_ambulance;
        int same_amb = b->queue[side].head && b->queue[side].head->is_ambulance;

        if (opp_amb) {
            printf("[BRIDGE] PRIORITY: ambulance waiting %s\n",
                   opp == EAST ? "EAST" : "WEST");
            fflush(stdout);
            try_wake_head(b, opp);
        } else if (same_amb) {
            printf("[BRIDGE] PRIORITY: ambulance waiting %s\n",
                   side == EAST ? "EAST" : "WEST");
            fflush(stdout);
            try_wake_head(b, side);
        } else if (b->queue[opp].head) {
            printf("[BRIDGE] Switching direction to %s\n",
                   opp == EAST ? "EAST" : "WEST");
            fflush(stdout);
            try_wake_head(b, opp);
        } else if (b->queue[side].head) {
            try_wake_head(b, side);
        }

        emit_direction(b);
    }

    pthread_mutex_unlock(&b->lock);
}

/* ===================================================== */
/* ================ SEMAPHORE INTERFACE ================ */
/* ===================================================== */

void bridge_set_light(Bridge *b, Direction green_side)
{
    Direction red_side = (green_side == EAST) ? WEST : EAST;

    pthread_mutex_lock(&b->lock);

    b->light[green_side] = LIGHT_GREEN;
    b->light[red_side]   = LIGHT_RED;

    /* Human-readable log line */
    printf("[SEMAPHORE] Light GREEN for %s, RED for %s\n",
           green_side == EAST ? "EAST" : "WEST",
           red_side   == EAST ? "EAST" : "WEST");

    /* Structured lines for the GUI — one per side */
    printf("[LIGHT %s GREEN]\n", green_side == EAST ? "EAST" : "WEST");
    printf("[LIGHT %s RED]\n",   red_side   == EAST ? "EAST" : "WEST");
    fflush(stdout);

    try_wake_head(b, green_side);
    try_wake_head(b, red_side);

    pthread_mutex_unlock(&b->lock);
}

/* ===================================================== */
/* ================ UTILITY FUNCTIONS ================== */
/* ===================================================== */

int bridge_get_length(Bridge *bridge)
{
    return bridge ? bridge->length : -1;
}