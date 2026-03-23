#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "officer_ctrl.h"
#include "bridge.h"
#include "config.h"

/* ===================================================== */
/* ================ OFFICER THREAD ==================== */
/* ===================================================== */

/*
 * Arguments passed to each officer thread at creation.
 * Heap-allocated and freed inside the thread itself.
 */
typedef struct {
    OfficerCtrl *ctrl;
    Direction    side;
    int          k_value;
} OfficerThreadArgs;

/*
 * Shared thread routine for both the EAST and WEST officer threads.
 *
 * Locking model
 * -------------
 * The thread acquires bridge->lock at startup and holds it for its
 * entire lifetime.  It is only ever *released* during the two
 * pthread_cond_wait calls — which is exactly the purpose of condition
 * variables.  Vehicle threads therefore run freely while the officer
 * thread is waiting; they just cannot run simultaneously with it during
 * the brief moments it is active (setting officer state, evaluating
 * conditions, switching turns).
 *
 * This avoids any busy-waiting and guarantees that all state changes
 * (officer_turn, current_k_value, ambulance_reset) are always observed
 * consistently.
 */
static void *officer_side_thread(void *arg)
{
    OfficerThreadArgs *args   = (OfficerThreadArgs *)arg;
    OfficerCtrl       *ctrl   = args->ctrl;
    Bridge            *bridge = ctrl->bridge;
    Direction          my_side = args->side;
    int                my_k    = args->k_value;
    Direction          opp     = (my_side == EAST) ? WEST : EAST;

    free(args);   /* args were heap-allocated in officer_ctrl_start */

    printf("[OFFICER %s] Thread started (k=%d)\n",
           my_side == EAST ? "EAST" : "WEST", my_k);

    pthread_mutex_lock(&bridge->lock);

    while (ctrl->running)
    {
        /* ---- Phase 1: wait until it is our turn ---- */
        while (bridge->officer_turn != my_side && ctrl->running)
            pthread_cond_wait(&bridge->officer_done_cv, &bridge->lock);

        if (!ctrl->running) break;

        /*
         * ---- Phase 2: run our phase ----
         *
         * Open our side with a fresh K quota and wait until one of:
         *   (a) K reaches 0               → switch turn to the other side
         *   (b) ambulance_reset fires      → ambulance from the blocked side
         *                                   crossed; our turn ends immediately,
         *                                   switch turn to the other side
         *   (c) early-switch condition     → switch turn early
         *   (d) shutdown requested         → exit
         *
         * pthread_cond_wait releases bridge->lock while sleeping,
         * so vehicle threads run freely during this wait.
         */
        bridge_set_officer(bridge, my_side, my_k);

        while (ctrl->running &&
               (bridge->current_k_value > 0 || bridge->k_on_bridge > 0) &&
               !bridge->ambulance_reset)
        {
            /*
             * Early-switch check:
             * If the quota is not yet exhausted but there are no vehicles
             * on our side (on the bridge or waiting) and the other side
             * has vehicles, give them the bridge immediately.
             */
            if (bridge->cars_on_bridge == 0 &&
                bridge->queue[my_side].size == 0 &&
                bridge->queue[opp].size > 0)
            {
                printf("[OFFICER %s] No vehicles on my side — "
                       "early switch to %s.\n",
                       my_side == EAST ? "EAST" : "WEST",
                       opp     == EAST ? "EAST" : "WEST");
                break;
            }

            pthread_cond_wait(&bridge->officer_cv, &bridge->lock);
        }

        if (!ctrl->running) break;

        if (bridge->ambulance_reset)
        {
            printf("[OFFICER %s] Ambulance crossed from blocked side — "
                   "turn passes to %s.\n",
                   my_side == EAST ? "EAST" : "WEST",
                   opp     == EAST ? "EAST" : "WEST");
        }

        if (!ctrl->running) break;

        /* ---- Phase 3: pass the turn to the other side ---- */
        printf("[OFFICER %s] Phase complete (k=%d, reset=%d). "
               "Passing turn to %s.\n",
               my_side == EAST ? "EAST" : "WEST",
               bridge->current_k_value,
               bridge->ambulance_reset,
               opp == EAST ? "EAST" : "WEST");

        bridge->officer_turn = opp;
        pthread_cond_broadcast(&bridge->officer_done_cv);
    }

    pthread_mutex_unlock(&bridge->lock);

    printf("[OFFICER %s] Thread stopped.\n",
           my_side == EAST ? "EAST" : "WEST");
    return NULL;
}

/* ===================================================== */
/* ================ PUBLIC INTERFACE ================== */
/* ===================================================== */

void officer_ctrl_start(OfficerCtrl  *ctrl,
                        Bridge       *bridge,
                        const Config *config)
{
    ctrl->bridge  = bridge;
    ctrl->config  = config;
    ctrl->running = 1;

    /* bridge->officer_turn is initialised to EAST in bridge_create,
       so the EAST thread goes first automatically. */

    OfficerThreadArgs *east_args = malloc(sizeof(OfficerThreadArgs));
    east_args->ctrl    = ctrl;
    east_args->side    = EAST;
    east_args->k_value = config->east.k_value;

    OfficerThreadArgs *west_args = malloc(sizeof(OfficerThreadArgs));
    west_args->ctrl    = ctrl;
    west_args->side    = WEST;
    west_args->k_value = config->west.k_value;

    pthread_create(&ctrl->east_thread, NULL, officer_side_thread, east_args);
    pthread_create(&ctrl->west_thread, NULL, officer_side_thread, west_args);

    printf("[OFFICER] Controller started. East k=%d, West k=%d\n",
           config->east.k_value, config->west.k_value);
}

void officer_ctrl_stop(OfficerCtrl *ctrl)
{
    ctrl->running = 0;

    /*
     * Broadcast on both CVs so that whichever thread is currently
     * sleeping (on officer_done_cv or officer_cv) wakes up, sees
     * running=0, and exits its loop.
     */
    pthread_mutex_lock(&ctrl->bridge->lock);
    pthread_cond_broadcast(&ctrl->bridge->officer_done_cv);
    pthread_cond_broadcast(&ctrl->bridge->officer_cv);
    pthread_mutex_unlock(&ctrl->bridge->lock);

    pthread_join(ctrl->east_thread, NULL);
    pthread_join(ctrl->west_thread, NULL);

    printf("[OFFICER] Controller stopped.\n");
}