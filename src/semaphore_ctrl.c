#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "semaphore_ctrl.h"
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

static void *semaphore_thread(void *arg)
{
    SemaphoreCtrl *ctrl   = (SemaphoreCtrl *)arg;
    Bridge        *bridge = ctrl->bridge;
    const Config  *config = ctrl->config;

    int east_green = config->east.green_time;
    int west_green = config->west.green_time;

    printf("[SEMAPHORE] Controller started. "
           "East green: %ds, West green: %ds\n",
           east_green, west_green);

    /* Start with EAST green */
    bridge_set_light(bridge, EAST);

    while (ctrl->running) {
        /* --- EAST green phase --- */
        for (int t = 0; t < east_green && ctrl->running; t++)
            sleep(1);

        if (!ctrl->running) break;

        /* Switch to WEST green */
        bridge_set_light(bridge, WEST);

        /* --- WEST green phase --- */
        for (int t = 0; t < west_green && ctrl->running; t++)
            sleep(1);

        if (!ctrl->running) break;

        /* Switch back to EAST green */
        bridge_set_light(bridge, EAST);
    }

    printf("[SEMAPHORE] Controller stopped.\n");
    return NULL;
}

void semaphore_ctrl_start(SemaphoreCtrl *ctrl,
                          Bridge        *bridge,
                          const Config  *config)
{
    ctrl->bridge  = bridge;
    ctrl->config  = config;
    ctrl->running = 1;
    pthread_create(&ctrl->thread, NULL, semaphore_thread, ctrl);
}

void semaphore_ctrl_stop(SemaphoreCtrl *ctrl)
{
    ctrl->running = 0;
    pthread_join(ctrl->thread, NULL);
}