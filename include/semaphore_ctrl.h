#ifndef SEMAPHORE_CTRL_H
#define SEMAPHORE_CTRL_H

#include "bridge.h"
#include "config.h"

/*
 * Semaphore controller thread.
 *
 * Runs only when config->mode == MODE_SEMAPHORE.
 * Cycles the traffic lights independently for each side using the
 * green_time values from the config.  The thread exits cleanly when
 * semaphore_ctrl_stop() is called.
 */

typedef struct {
    Bridge       *bridge;
    const Config *config;
    pthread_t     thread;
    volatile int  running;  /* set to 0 to request shutdown */
} SemaphoreCtrl;

/* Start the controller (spawns its thread). */
void semaphore_ctrl_start(SemaphoreCtrl *ctrl,
                          Bridge        *bridge,
                          const Config  *config);

/* Signal the controller to stop and join its thread. */
void semaphore_ctrl_stop(SemaphoreCtrl *ctrl);

#endif