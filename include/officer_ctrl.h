#ifndef OFFICER_CTRL_H
#define SOFFICER_CTRL_H

#include "bridge.h"
#include "config.h"

/*
 * Officer controller thread.
 *
 * Runs only when config->mode == MODE_OFFICER.
 * Cycles the traffic lights by the config of east_k_value
 * and west_k_value.  The thread exits cleanly when
 * officer_ctrl_stop() is called.
 */

typedef struct {
    Bridge       *bridge;
    const Config *config;
    pthread_t     thread;
    volatile int  running;  /* set to 0 to request shutdown */
} OfficerCtrl;

/* Start the controller (spawns its thread). */
void officer_ctrl_start(OfficerCtrl *ctrl,
                          Bridge        *bridge,
                          const Config  *config);

/* Signal the controller to stop and join its thread. */
void officer_ctrl_stop(OfficerCtrl *ctrl);

#endif