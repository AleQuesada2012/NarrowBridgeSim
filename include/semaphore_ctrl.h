#ifndef SEMAPHORE_CTRL_H
#define SEMAPHORE_CTRL_H

#include <pthread.h>
#include "bridge.h"
#include "config.h"

/*
 * Two independent semaphore threads, one per side of the bridge.
 *
 * Each Semaphore thread owns its own condition variable and mutex.
 * The cycle is:
 *
 *   1. Wait on own cv until the peer signals "it's your turn".
 *   2. Call bridge_set_light() for this side (goes GREEN, peer goes RED).
 *   3. Sleep for green_time seconds (wall-clock, 1-second ticks for
 *      responsive shutdown).
 *   4. Signal the peer's cv  →  peer wakes and repeats from step 2.
 *   5. Go back to sleep (step 1).
 *
 * EAST starts green at boot: its thread self-signals during initialisation
 * so it skips the first wait and immediately becomes green.
 *
 * Shutdown: semaphore_ctrl_stop() sets running=0 on both Semaphore structs,
 * then broadcasts both cvs so any sleeping thread wakes, checks the flag,
 * and exits cleanly.
 */

/* One semaphore (one physical traffic light, one thread). */
typedef struct Semaphore {
    pthread_t        thread;
    pthread_mutex_t  mutex;
    pthread_cond_t   cv;
    int              go;          /* 1 = woken and allowed to become green */

    Direction        side;        /* EAST or WEST                           */
    int              green_time;  /* seconds to hold the green phase        */

    Bridge          *bridge;
    volatile int    *running;     /* shared flag — points into SemaphoreCtrl */
    struct Semaphore *peer;       /* the other semaphore thread              */
} Semaphore;

/* Top-level controller owned by main(). */
typedef struct {
    Semaphore east;
    Semaphore west;
    volatile int running;
} SemaphoreCtrl;

/* Start both semaphore threads. */
void semaphore_ctrl_start(SemaphoreCtrl *ctrl,
                          Bridge        *bridge,
                          const Config  *config);

/* Stop both semaphore threads and join them. */
void semaphore_ctrl_stop(SemaphoreCtrl *ctrl);

#endif