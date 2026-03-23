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
 * Must be called with bridge->lock held.
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
 *   Normal car on SAME side:   Carnage rules, while k > 0.
 *   Normal car on OPP side:    blocked entirely.
 *   Ambulance on SAME side:    Carnage rules (does not consume a K slot).
 *   Ambulance on OPP side:     may enter only when bridge is completely empty.
 */
static int can_head_enter(const Bridge *b, Direction side)
{
    const FifoNode *head = b->queue[side].head;
    if (!head)
        return 0;

    Direction opp = (side == EAST) ? WEST : EAST;
    const FifoNode *opp_head = b->queue[opp].head;

    /* Hard capacity check (all modes) */
    if (b->cars_on_bridge >= b->length) return 0;

    /* Direction / oncoming traffic check (all modes) */
    int bridge_clear    = (b->cars_on_bridge == 0);
    int same_direction  = (b->current_direction == side);
    int bridge_ok       = bridge_clear || same_direction;

    /* Opposite-ambulance yield rule (all modes) */
    int must_yield = (!head->is_ambulance &&
                      opp_head != NULL &&
                      opp_head->is_ambulance);

    /* ---- SEMAPHORE ---- */
    if (b->mode == MODE_SEMAPHORE)
    {
        LightState my_light = b->light[side];

        if (head->is_ambulance)
            /* Ambulance on RED: enter only when bridge is clear */
            return bridge_ok && !must_yield;

        /* Normal car on red: always blocked */
        if (my_light == LIGHT_RED)
            return 0;

        /* Normal car on green: Carnage rules */
        return bridge_ok && !must_yield;
    }

    /* ---- OFFICER ---- */
    if (b->mode == MODE_OFFICER)
    {
        OfficerState my_side = b->officer_side[side];

        if (head->is_ambulance)
            /*
             * Ambulance on OPP side: enter only when bridge is completely
             * empty (bridge_ok is false if cars from the other direction
             * are still crossing).  Ambulance on SAME side: Carnage rules.
             */
            return bridge_ok;

        /* Normal car on the blocked side: always wait */
        if (my_side == OPP || my_side == OFFICERs_DAY_OFF)
            return 0;

        /* Normal car on the active side: Carnage rules while k > 0 */
        return bridge_ok && !must_yield && (b->current_k_value > 0);
    }

    /* ---- CARNAGE ---- */
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
    if (!b) return NULL;

    b->length = config->bridge_length;
    b->mode   = config->mode;

    b->slots = malloc(sizeof(pthread_mutex_t) * b->length);
    for (int i = 0; i < b->length; i++)
        pthread_mutex_init(&b->slots[i], NULL);

    pthread_mutex_init(&b->lock, NULL);

    b->cars_on_bridge  = 0;
    b->current_direction = NONE;

    for (int s = 0; s < 2; s++)
    {
        b->queue[s].head        = NULL;
        b->queue[s].tail        = NULL;
        b->queue[s].size        = 0;
        b->waiting[s]           = 0;
        b->ambulances_waiting[s] = 0;
        b->passed_count[s]      = 0;
        b->light[s]             = LIGHT_OFF;
        b->officer_side[s]      = OFFICERs_DAY_OFF;
    }

    /* Officer-mode fields */
    b->current_k_value = 0;
    b->ambulance_reset = 0;
    b->officer_turn    = EAST;   /* EAST officer thread goes first */

    pthread_cond_init(&b->officer_cv,      NULL);
    pthread_cond_init(&b->officer_done_cv, NULL);

    printf("[BRIDGE] Created with length: %d meters.\n", b->length);
    return b;
}

void bridge_destroy(Bridge *b)
{
    for (int i = 0; i < b->length; i++)
        pthread_mutex_destroy(&b->slots[i]);
    free(b->slots);

    pthread_cond_destroy(&b->officer_cv);
    pthread_cond_destroy(&b->officer_done_cv);
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
    FifoQueue *q   = &b->queue[side];

    /* Stack-allocate the wait node for this vehicle */
    FifoNode node;
    pthread_cond_init(&node.cv, NULL);
    node.is_ambulance = info->is_ambulance;
    node.id           = info->id;
    node.next         = NULL;

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
     * need to yield. Signal it so it re-evaluates its wait condition.
     */
    if (info->is_ambulance)
    {
        Direction opp = (side == EAST) ? WEST : EAST;
        if (b->queue[opp].head)
            pthread_cond_signal(&b->queue[opp].head->cv);
    }

    /*
     * OFFICER mode: if a normal car arrives on the currently-blocked
     * side, wake the officer so it can check for an early switch
     * (bridge already empty + active side has no one waiting).
     */
    if (b->mode == MODE_OFFICER &&
        !info->is_ambulance &&
        b->officer_side[side] == OPP)
    {
        pthread_cond_signal(&b->officer_cv);
    }

    /* Wait until we are at the head AND can_head_enter says yes */
    while (q->head != &node || !can_head_enter(b, side))
        pthread_cond_wait(&node.cv, &b->lock);

    /* Grab slot 0 before releasing the bridge lock (prevents race at entry) */
    pthread_mutex_lock(&b->slots[0]);

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

    /* SEMAPHORE mode: log ambulance crossing on red */
    if (b->mode == MODE_SEMAPHORE &&
        info->is_ambulance &&
        b->light[side] == LIGHT_RED)
    {
        printf("[SEMAPHORE] Ambulance %d crossing on RED (%s). "
               "Light timer unchanged.\n",
               info->id, side == EAST ? "EAST" : "WEST");
    }

    /*
     * OFFICER mode: an ambulance from the blocked side crosses.
     * Set ambulance_reset so the active officer thread wakes up,
     * ends its phase early, and passes the turn to the other side.
     */
    if (b->mode == MODE_OFFICER &&
        info->is_ambulance &&
        b->officer_side[side] == OPP)
    {
        printf("[OFFICER] Ambulance %d crossing from BLOCKED side (%s). "
               "Ending active side's turn.\n",
               info->id, side == EAST ? "EAST" : "WEST");
        b->ambulance_reset = 1;
        pthread_cond_signal(&b->officer_cv);
    }

    /*
     * Only non-ambulance vehicles on the ACTIVE side consume a K slot.
     * Ambulances (same or opposite side) are priority passes and do
     * not count toward the K limit.
     */
    if (b->mode == MODE_OFFICER &&
        !info->is_ambulance &&
        b->officer_side[side] == SAME)
    {
        decrement_k_and_notify(b);
    }

    /* Wake the new head of our queue (same-direction pipelining) */
    try_wake_head(b, side);

    /* Wake the opposite head in case an ambulance unblocked it */
    Direction opp = (side == EAST) ? WEST : EAST;
    try_wake_head(b, opp);

    pthread_mutex_unlock(&b->lock);

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
         * try_wake_head respects light/officer state, so blocked vehicles
         * will not be woken here — only when the controller enables them.
         */
        int opp_amb  = b->queue[opp].head  && b->queue[opp].head->is_ambulance;
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

        /*
         * OFFICER mode: signal the active officer thread so it can check
         * the early-switch condition (bridge empty, active side queue also
         * empty, other side has vehicles waiting).
         */
        if (b->mode == MODE_OFFICER)
            pthread_cond_signal(&b->officer_cv);
    }
    else
    {
        /*
         * Bridge not empty but a slot freed up. Wake same-direction head
         * in case it was blocked only by the capacity limit.
         */
        try_wake_head(b, side);
    }

    pthread_mutex_unlock(&b->lock);
}

/* ===================================================== */
/* ================ SEMAPHORE INTERFACE ================ */
/* ===================================================== */

/*
 * Called by the semaphore controller thread to flip the lights.
 * Acquires bridge->lock internally.
 */
void bridge_set_light(Bridge *b, Direction green_side)
{
    Direction red_side = (green_side == EAST) ? WEST : EAST;

    pthread_mutex_lock(&b->lock);

    b->light[green_side] = LIGHT_GREEN;
    b->light[red_side]   = LIGHT_RED;

    printf("[SEMAPHORE] Light GREEN for %s, RED for %s\n",
           green_side == EAST ? "EAST" : "WEST",
           red_side   == EAST ? "EAST" : "WEST");

    /* Wake both heads — can_head_enter decides who actually enters */
    try_wake_head(b, green_side);
    try_wake_head(b, red_side);   /* red ambulance may cross if bridge empty */

    pthread_mutex_unlock(&b->lock);
}

/* ===================================================== */
/* ================ OFFICER INTERFACE  ================= */
/* ===================================================== */

/*
 * Activates `new_side` with a fresh quota of `k` vehicles.
 *
 * IMPORTANT: must be called with bridge->lock already held.
 * Does NOT acquire or release the lock.
 */
void bridge_set_officer(Bridge *b, Direction new_side, int k)
{
    Direction opp_side = (new_side == EAST) ? WEST : EAST;

    b->current_k_value          = k;
    b->officer_side[new_side]   = SAME;
    b->officer_side[opp_side]   = OPP;
    b->ambulance_reset          = 0;

    printf("[OFFICER] BRIDGE OPEN for %s (k=%d), CLOSED for %s\n",
           new_side == EAST ? "EAST" : "WEST", k,
           opp_side == EAST ? "EAST" : "WEST");

    /* Wake both heads — can_head_enter will sort out who actually enters */
    try_wake_head(b, new_side);
    try_wake_head(b, opp_side);   /* OPP ambulance may still cross if bridge empty */
}

/* ===================================================== */
/* ================ UTILITY FUNCTIONS ================== */
/* ===================================================== */

int bridge_get_length(Bridge *bridge)
{
    return bridge ? bridge->length : -1;
}

/*
 * Decrements the K counter and notifies the officer thread when the
 * quota hits zero.  Called only for non-ambulance vehicles on the SAME
 * officer side (enforced at the call site in bridge_enter).
 */
void decrement_k_and_notify(Bridge *bridge)
{
    if (bridge->current_k_value <= 0) return;

    bridge->current_k_value--;
    printf("[OFFICER] K slot used. Remaining: %d\n", bridge->current_k_value);

    if (bridge->current_k_value == 0)
    {
        printf("[OFFICER] K exhausted — waking officer thread.\n");
        pthread_cond_signal(&bridge->officer_cv);
    }
}