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
/* ================ ENTRY PREDICATE ==================== */
/* ===================================================== */

/*
 * Decides whether the head of `side` may enter the bridge right now.
 * This is the single place where Carnage, Semaphore (and later Officer)
 * rules diverge. Must be called with bridge->lock held.
 *
 * CARNAGE
 *   - Bridge empty or same direction, AND
 *   - Not a normal car facing an ambulance at the opposite head.
 *
 * SEMAPHORE
 *   Normal car on GREEN: same as Carnage.
 *   Normal car on RED:   blocked entirely — must wait for green.
 *   Ambulance on GREEN:  same as Carnage.
 *   Ambulance on RED:    may enter only when the bridge is completely
 *                        empty (no oncoming cars). The light does not
 *                        flip; the ambulance simply crosses on red.
 * 
 * OFFICER
 *  Normal car on current direction:    same as Carnage until enough vehicules pass the bridge.
 *  Normal car on opposite direction:   blocked entirely — must wait for the officer.
 *  Ambulance on current direction:     doesn't count in the "Officer" count, so same as Carnage.
 *  Ambulance on opposite direction:    will enter when is the first in the queue,
 *                                      otherwise should wait for its turn.
 * 
 */
static int can_head_enter(const Bridge *b, Direction side)
{
    const FifoNode *head = b->queue[side].head;
    if (!head)
        return 0;

    Direction opp = (side == EAST) ? WEST : EAST;
    const FifoNode *opp_head = b->queue[opp].head;

    /* ---- direction / oncoming traffic check (all modes) ---- */
    int bridge_clear = (b->cars_on_bridge == 0);
    int same_direction = (b->current_direction == side);
    int bridge_ok = bridge_clear || same_direction;

    /* ---- opposite-ambulance yield rule (all modes) ---- */
    int must_yield = (!head->is_ambulance &&
                      opp_head != NULL &&
                      opp_head->is_ambulance);

    if (b->mode == MODE_SEMAPHORE)
    {
        LightState my_light = b->light[side];

        if (head->is_ambulance)
        {
            /*
             * Ambulance on red: cross only when the bridge is completely
             * empty. The must_yield rule still applies — if the opposite
             * head is also an ambulance and the bridge is empty, both
             * can_head_enter calls return true and the one that wins the
             * lock first enters (carnage-style among ambulances).
             */
            if (my_light == LIGHT_RED)
                return bridge_clear && !must_yield;

            /* Ambulance on green: normal carnage rules apply */
            return bridge_ok && !must_yield;
        }

        /* Normal car on red: always blocked */
        if (my_light == LIGHT_RED)
            return 0;

        /* Normal car on green: carnage rules */
        return bridge_ok && !must_yield;
    }

    /*TODO: Add the conditions for a vehicule in Officer mode*/

    /* CARNAGE (and OFFICER placeholder): direction + yield rules */
    return bridge_ok && !must_yield;
}

/*
 * Signal the head of `side` if it can now enter.
 * Must be called with bridge->lock held.
 */
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
    b->mode = config->mode;

    b->slots = malloc(sizeof(pthread_mutex_t) * b->length);
    for (int i = 0; i < b->length; i++)
        pthread_mutex_init(&b->slots[i], NULL);

    pthread_mutex_init(&b->lock, NULL);

    b->cars_on_bridge = 0;
    b->current_direction = NONE;

    for (int s = 0; s < 2; s++)
    {
        b->queue[s].head = NULL;
        b->queue[s].tail = NULL;
        b->queue[s].size = 0;
        b->waiting[s] = 0;
        b->ambulances_waiting[s] = 0;
        b->passed_count[s] = 0;
        b->light[s] = LIGHT_OFF;
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
    Direction side = info->direction;
    FifoQueue *q = &b->queue[side];

    FifoNode node;
    pthread_cond_init(&node.cv, NULL);
    node.is_ambulance = info->is_ambulance;
    node.id = info->id;
    node.next = NULL;

    pthread_mutex_lock(&b->lock);

    b->waiting[side]++;
    if (info->is_ambulance)
        b->ambulances_waiting[side]++;

    fifo_push(q, &node);

    printf("[VEHICLE %d] Arrived at bridge headed %s%s. Queue: %d waiting\n",
           info->id,
           side == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "",
           q->size);

    /*
     * If a new ambulance just joined our queue, the opposite head might
     * now need to yield. Signal it so it re-evaluates its wait condition.
     */
    if (info->is_ambulance)
    {
        Direction opp = (side == EAST) ? WEST : EAST;
        if (b->queue[opp].head)
            pthread_cond_signal(&b->queue[opp].head->cv);
    }

    /*
     * Wait until we are at the head AND can_head_enter says yes.
     */
    while (q->head != &node || !can_head_enter(b, side))
        pthread_cond_wait(&node.cv, &b->lock);

    /* --- Cleared to enter --- */
    fifo_pop(q);

    b->waiting[side]--;
    if (info->is_ambulance)
        b->ambulances_waiting[side]--;

    b->cars_on_bridge++;
    b->current_direction = side;
    b->passed_count[side]++;

    printf("[BRIDGE] Vehicle %d entered headed %s%s. Cars on bridge: %d\n",
           info->id,
           side == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "",
           b->cars_on_bridge);

    /* Semaphore mode: note when an ambulance crosses on red */
    if (b->mode == MODE_SEMAPHORE &&
        info->is_ambulance &&
        b->light[side] == LIGHT_RED)
    {
        printf("[SEMAPHORE] Ambulance %d crossing on RED (%s). "
               "Light timer unchanged.\n",
               info->id, side == EAST ? "EAST" : "WEST");
    }

    /*TODO: add the case when an ambulance arrives as the head of the queue but is in the opp direction*/

    /*
     * Wake the new head of our queue — it may also be able to enter
     * (same-direction pipelining, carnage-style when green).
     */
    try_wake_head(b, side);

    /*
     * Wake the opposite head in case it was blocked by us being an
     * ambulance and can now re-evaluate (e.g. if it is also an ambulance).
     */
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
    Direction opp = (side == EAST) ? WEST : EAST;

    pthread_mutex_unlock(&b->slots[b->length - 1]);

    pthread_mutex_lock(&b->lock);

    b->cars_on_bridge--;

    printf("[BRIDGE] Vehicle %d exited going %s. Cars remaining: %d\n",
           info->id,
           side == EAST ? "EAST" : "WEST",
           b->cars_on_bridge);

    if (b->cars_on_bridge == 0)
    {
        b->current_direction = NONE;

        /*
         * Bridge empty — decide who to wake.
         *
         * Priority order (same for all modes):
         *   1. Ambulance at opposite head
         *   2. Ambulance at same head
         *   3. Any vehicle at opposite head  (fairness)
         *   4. Any vehicle at same head
         *
         * try_wake_head respects the light state, so in SEMAPHORE mode
         * a normal car on red will not be woken here — it will be woken
         * when the semaphore thread flips the light to green.
         */
        int opp_amb = b->queue[opp].head && b->queue[opp].head->is_ambulance;
        int same_amb = b->queue[side].head && b->queue[side].head->is_ambulance;

        if (opp_amb)
        {
            printf("[BRIDGE] PRIORITY: ambulance waiting %s\n",
                   opp == EAST ? "EAST" : "WEST");
            try_wake_head(b, opp);
        }
        else if (same_amb)
        {
            printf("[BRIDGE] PRIORITY: ambulance waiting %s\n",
                   side == EAST ? "EAST" : "WEST");
            try_wake_head(b, side);
        }
        else if (b->queue[opp].head)
        {
            printf("[BRIDGE] Switching direction to %s\n",
                   opp == EAST ? "EAST" : "WEST");
            try_wake_head(b, opp);
        }
        else if (b->queue[side].head)
        {
            try_wake_head(b, side);
        }
    }

    pthread_mutex_unlock(&b->lock);
}

/* ===================================================== */
/* ================ SEMAPHORE INTERFACE ================ */
/* ===================================================== */

/*
 * Called by the semaphore controller thread to flip the lights.
 * green_side becomes GREEN; the other side becomes RED.
 * Both queue heads are poked so they re-evaluate their conditions:
 *   - The newly-green head may now be able to enter.
 *   - The newly-red head must go back to sleep (if it is a normal car).
 */
void bridge_set_light(Bridge *b, Direction green_side)
{
    Direction red_side = (green_side == EAST) ? WEST : EAST;

    pthread_mutex_lock(&b->lock);

    b->light[green_side] = LIGHT_GREEN;
    b->light[red_side] = LIGHT_RED;

    printf("[SEMAPHORE] Light GREEN for %s, RED for %s\n",
           green_side == EAST ? "EAST" : "WEST",
           red_side == EAST ? "EAST" : "WEST");

    /* Wake both heads — can_head_enter will sort out who actually enters */
    try_wake_head(b, green_side);
    try_wake_head(b, red_side); /* red ambulance may still enter if bridge empty */

    pthread_mutex_unlock(&b->lock);
}

/*TODO: Officer interface*/

/* ===================================================== */
/* ================ UTILITY FUNCTIONS ================== */
/* ===================================================== */

int bridge_get_length(Bridge *bridge)
{
    return bridge ? bridge->length : -1;
}