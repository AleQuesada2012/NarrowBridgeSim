#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "semaphore_ctrl.h"
#include "bridge.h"
#include "config.h"

/* ===================================================== */
/* ================ SEMAPHORE THREAD =================== */
/* ===================================================== */

static void *semaphore_thread(void *arg)
{
    Semaphore *sem = (Semaphore *)arg;
    const char *name = (sem->side == EAST) ? "EAST" : "WEST";

    printf("[SEMAPHORE-%s] Thread started. Green time: %ds\n",
           name, sem->green_time);

    for (;;) {
        /* ---- Step 1: wait until the peer signals us ---- */
        pthread_mutex_lock(&sem->mutex);
        while (!sem->go && *sem->running)
            pthread_cond_wait(&sem->cv, &sem->mutex);

        sem->go = 0;   /* consume the token */
        pthread_mutex_unlock(&sem->mutex);

        /* Exit if shutdown was requested while we were sleeping */
        if (!*sem->running)
            break;

        /* ---- Step 2: become green ---- */
        bridge_set_light(sem->bridge, sem->side);

        printf("[SEMAPHORE-%s] Now GREEN for %ds\n", name, sem->green_time);

        /* ---- Step 3: hold green for green_time seconds ---- */
        for (int t = 0; t < sem->green_time && *sem->running; t++)
            sleep(1);

        if (!*sem->running)
            break;

        printf("[SEMAPHORE-%s] Green phase over. Waking %s.\n", name,
        (sem->side == EAST) ? "WEST" : "EAST");

        /* ---- Step 4: signal peer — it is now its turn ---- */
        pthread_mutex_lock(&sem->peer->mutex);
        sem->peer->go = 1;
        pthread_cond_signal(&sem->peer->cv);
        pthread_mutex_unlock(&sem->peer->mutex);

        /* ---- Step 5: go back to sleep (loop to Step 1) ---- */
    }

    printf("[SEMAPHORE-%s] Thread stopped.\n", name);
    return NULL;
}

/* ===================================================== */
/* ================ LIFECYCLE ========================== */
/* ===================================================== */

static void semaphore_init(Semaphore    *sem,
                           Direction     side,
                           int           green_time,
                           Bridge       *bridge,
                           volatile int *running)
{
    sem->side       = side;
    sem->green_time = green_time;
    sem->bridge     = bridge;
    sem->running    = running;
    sem->go         = 0;
    sem->peer       = NULL;   /* wired up by semaphore_ctrl_start */

    pthread_mutex_init(&sem->mutex, NULL);
    pthread_cond_init(&sem->cv,     NULL);
}

void semaphore_ctrl_start(SemaphoreCtrl *ctrl,
                          Bridge        *bridge,
                          const Config  *config)
{
    ctrl->running = 1;

    semaphore_init(&ctrl->east, EAST,
                   config->east.green_time, bridge, &ctrl->running);
    semaphore_init(&ctrl->west, WEST,
                   config->west.green_time, bridge, &ctrl->running);

    /* Wire the peers so each thread can signal the other */
    ctrl->east.peer = &ctrl->west;
    ctrl->west.peer = &ctrl->east;

    /*
     * Bootstrap: give EAST the first token so it becomes green
     * immediately when its thread starts, without waiting for a signal.
     */
    ctrl->east.go = 1;

    printf("[SEMAPHORE] Starting two independent semaphore threads.\n"
           "[SEMAPHORE] East green: %ds  West green: %ds\n",
           config->east.green_time, config->west.green_time);

    pthread_create(&ctrl->east.thread, NULL, semaphore_thread, &ctrl->east);
    pthread_create(&ctrl->west.thread, NULL, semaphore_thread, &ctrl->west);
}

void semaphore_ctrl_stop(SemaphoreCtrl *ctrl)
{
    /* Signal shutdown to both threads */
    ctrl->running = 0;

    /*
     * Broadcast on both cvs so neither thread sleeps forever waiting
     * for a token that will never come.
     */
    pthread_mutex_lock(&ctrl->east.mutex);
    pthread_cond_broadcast(&ctrl->east.cv);
    pthread_mutex_unlock(&ctrl->east.mutex);

    pthread_mutex_lock(&ctrl->west.mutex);
    pthread_cond_broadcast(&ctrl->west.cv);
    pthread_mutex_unlock(&ctrl->west.mutex);

    pthread_join(ctrl->east.thread, NULL);
    pthread_join(ctrl->west.thread, NULL);

    pthread_cond_destroy(&ctrl->east.cv);
    pthread_mutex_destroy(&ctrl->east.mutex);
    pthread_cond_destroy(&ctrl->west.cv);
    pthread_mutex_destroy(&ctrl->west.mutex);

    printf("[SEMAPHORE] Both threads joined.\n");
}