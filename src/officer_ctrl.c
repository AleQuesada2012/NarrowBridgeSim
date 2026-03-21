#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "officer_ctrl.h"
#include "bridge.h"
#include "config.h"

/*
 * The semaphore controller thread.
 *
 * Timing model
 * ------------
 * The lights alternate strictly: EAST green for east_green_time seconds,
 * then WEST green for west_green_time seconds, then repeat.
 *
 * The timer is purely wall-clock based:
 *   - It does NOT pause while the bridge is being cleared.
 *   - It does NOT reset when an ambulance crosses on red.
 *   - When the green timer expires the light flips immediately, even if
 *     cars are mid-crossing. Those cars finish; the new red-side head
 *     re-evaluates and waits (bridge_set_light wakes it to re-check).
 *
 * Shutdown
 * --------
 * The main thread sets ctrl->running = 0 and the loop exits after the
 * current sleep segment completes (at most one second of extra latency,
 * since we sleep in 1-second ticks to remain responsive to shutdown).
 */

static void *officer_thread(void *arg)
{
    OfficerCtrl *ctrl   = (OfficerCtrl *)arg;
    Bridge        *bridge = ctrl->bridge;
    const Config  *config = ctrl->config;

    int east_k_value = config->east.k_value;
    int west_k_value = config->west.k_value;

    printf("[OFFICER] Controller started. "
           "East k value: %ds, West k value: %ds\n",
           east_k_value, west_k_value);

    /* Start with EAST side */
    bridge_set_officer(bridge, EAST, east_k_value + 1);
    // adding 1 to the k values allows to do a cleaner switch if 
    // an ambulance arrives just when the k value is ruuning out
    while (ctrl->running) {
        /* --- EAST phase --- */
        while(bridge_get_current_k(bridge) > 0 && !bridge_get_ambulance_reset(bridge)){
        }

        if (!ctrl->running) break;

        /* Switch to WEST side */
        bridge_set_officer(bridge, WEST, west_k_value + 1);
        printf("%d",bridge->ambulance_reset);
        bridge->ambulance_reset = 0; // restoring the value for the next phase

        /* --- WEST phase --- */
        while(bridge_get_current_k(bridge) > 0 && !bridge_get_ambulance_reset(bridge)){
        }

        if (!ctrl->running) break;

        /* Switch back to EAST side */
        bridge->ambulance_reset = 0; // restoring the value for the next phase
        
        bridge_set_officer(bridge, EAST, east_k_value + 1);
    }

    printf("[OFFICER] Controller stopped.\n");
    return NULL;
}

void officer_ctrl_start(OfficerCtrl *ctrl,
                          Bridge        *bridge,
                          const Config  *config)
{
    ctrl->bridge  = bridge;
    ctrl->config  = config;
    ctrl->running = 1;
    pthread_create(&ctrl->thread, NULL, officer_thread, ctrl);
}

void officer_ctrl_stop(OfficerCtrl *ctrl)
{
    ctrl->running = 0;
    pthread_join(ctrl->thread, NULL);
}