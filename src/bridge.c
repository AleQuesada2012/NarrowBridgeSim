#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "bridge.h"

/* ===================================================== */
/* =================== FIFO HELPERS ==================== */
/* ===================================================== */

/* Append node to the tail — strict arrival order, no reordering. */
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

/* Remove the head node after it has been cleared to enter. */
static void fifo_pop(FifoQueue *q)
{
    if (!q->head) return;
    q->head = q->head->next;
    if (!q->head)
        q->tail = NULL;
    q->size--;
}

/* ===================================================== */
/* =================== BRIDGE LOGIC ==================== */
/* ===================================================== */

/*
 * Can the head of `side` enter the bridge right now?
 *
 * Yes if ALL of:
 *   (a) The bridge is empty or already flowing in this direction.
 *   (b) The head of the opposite queue is NOT an ambulance, OR this
 *       vehicle is itself an ambulance (two ambulances resolve by
 *       whoever is already on the bridge / direction order).
 *
 * Must be called with bridge->lock held.
 */
static int can_head_enter(const Bridge *b, Direction side)
{
    const FifoNode *head = b->queue[side].head;
    if (!head) return 0;

    Direction opp = (side == EAST) ? WEST : EAST;
    const FifoNode *opp_head = b->queue[opp].head;

    int bridge_ok  = (b->cars_on_bridge == 0 ||
                      b->current_direction == side);

    int must_yield = (!head->is_ambulance &&
                      opp_head != NULL &&
                      opp_head->is_ambulance);

    return bridge_ok && !must_yield;
}

/*
 * If the head of `side` can now enter, wake it.
 * Must be called with bridge->lock held.
 */
static void try_wake_head(Bridge *b, Direction side)
{
    if (can_head_enter(b, side))
        pthread_cond_signal(&b->queue[side].head->cv);
}

/* ===================================================== */
/* ================ BRIDGE LIFECYCLE =================== */
/* ===================================================== */

Bridge *bridge_create(const Config *config)
{
    Bridge *b = malloc(sizeof(Bridge));

    b->length = config->bridge_length;
    b->slots  = malloc(sizeof(pthread_mutex_t) * b->length);
    for (int i = 0; i < b->length; i++)
        pthread_mutex_init(&b->slots[i], NULL);

    pthread_mutex_init(&b->lock, NULL);

    b->cars_on_bridge    = 0;
    b->current_direction = NONE;

    for (int s = 0; s < 2; s++) {
        b->queue[s].head        = NULL;
        b->queue[s].tail        = NULL;
        b->queue[s].size        = 0;
        b->waiting[s]           = 0;
        b->ambulances_waiting[s] = 0;
        b->passed_count[s]      = 0;
    }

    printf("[BRIDGE] Created with length: %d meters.\n", b->length);
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

/* ===================================================== */
/* =============== SYNCHRONIZATION LOGIC =============== */
/* ===================================================== */

void bridge_enter(Bridge *b, BridgeVehicleInfo *info)
{
    Direction  side = info->direction;
    FifoQueue *q    = &b->queue[side];

    /* Build a wait node on this thread's stack. */
    FifoNode node;
    pthread_cond_init(&node.cv, NULL);
    node.is_ambulance = info->is_ambulance;
    node.id           = info->id;
    node.next         = NULL;

    pthread_mutex_lock(&b->lock);

    /* --- Join the tail of the FIFO --- */
    b->waiting[side]++;
    if (info->is_ambulance) b->ambulances_waiting[side]++;

    fifo_push(q, &node);

    printf("[VEHICLE %d] Arrived at bridge from %s%s. Queue: %d waiting\n",
           info->id,
           side == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "",
           q->size);

    /*
     * If a new ambulance just joined the tail, the opposite head may need
     * to stop entering. Signal it to re-check its wait condition — it will
     * go back to sleep if it now has to yield.
     *
     * Note: we only need to do this when we are an ambulance, because only
     * then does our presence change what the opposite head is allowed to do.
     */
    if (info->is_ambulance) {
        Direction opp = (side == EAST) ? WEST : EAST;
        if (b->queue[opp].head)
            pthread_cond_signal(&b->queue[opp].head->cv);
    }

    /*
     * Wait until:
     *   1. We are at the head of our FIFO (it is our turn), AND
     *   2. The bridge conditions allow us to enter.
     *
     * We use a straight cond_wait loop; the node at the head will be
     * explicitly signalled by either:
     *   - The vehicle ahead of us (when it enters the bridge and pops itself), or
     *   - bridge_leave (when the bridge empties and picks a new head to wake), or
     *   - An arriving opposite ambulance (to make it re-check and possibly sleep).
     */
    while (q->head != &node || !can_head_enter(b, side)) {
        pthread_cond_wait(&node.cv, &b->lock);
    }

    /* --- We are cleared to enter --- */
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

    /*
     * Wake the new head of our own queue: it is now at the front and
     * may be able to enter if conditions allow (carnage mode — multiple
     * same-direction vehicles can pipeline onto the bridge).
     */
    try_wake_head(b, side);

    /*
     * Also wake the opposite head in case it was waiting only because
     * we (now on the bridge) were blocking it, and it can now enter
     * (this only applies if it is an ambulance and we are too, which
     * can_head_enter handles correctly).
     */
    Direction opp = (side == EAST) ? WEST : EAST;
    try_wake_head(b, opp);

    pthread_mutex_unlock(&b->lock);

    /* Claim the first physical meter. */
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

    if (b->cars_on_bridge == 0) {
        b->current_direction = NONE;

        /*
         * Bridge is empty — decide who goes next and wake their head.
         *
         * Priority order:
         *   1. Ambulance waiting on the opposite side  (they have been blocked longest)
         *   2. Ambulance waiting on the same side
         *   3. Any vehicle on the opposite side        (fairness: alternate directions)
         *   4. Any vehicle on the same side
         */
        int opp_amb  = b->queue[opp].head  && b->queue[opp].head->is_ambulance;
        int same_amb = b->queue[side].head && b->queue[side].head->is_ambulance;

        if (opp_amb) {
            printf("[BRIDGE] PRIORITY: ambulance waiting %s\n",
                   opp == EAST ? "EAST" : "WEST");
            try_wake_head(b, opp);
        } else if (same_amb) {
            printf("[BRIDGE] PRIORITY: ambulance waiting %s\n",
                   side == EAST ? "EAST" : "WEST");
            try_wake_head(b, side);
        } else if (b->queue[opp].head) {
            printf("[BRIDGE] Switching direction to %s\n",
                   opp == EAST ? "EAST" : "WEST");
            try_wake_head(b, opp);
        } else if (b->queue[side].head) {
            try_wake_head(b, side);
        }
    }

    pthread_mutex_unlock(&b->lock);
}

/* ===================================================== */
/* ================ UTILITY FUNCTIONS ================== */
/* ===================================================== */

int bridge_get_length(Bridge *bridge)
{
    return bridge ? bridge->length : -1;
}