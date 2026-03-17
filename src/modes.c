#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "bridge.h"
#include "config.h"

/* ============================= */
/* ===== Internal Struct ======= */
/* ============================= */

typedef struct {
    Bridge *bridge;
    const Config *config;
} ModeArgs;


/* ============================= */
/* ===== CARNAGE MODE ========== */
/* ============================= */
/*
   No controller logic needed.
   Vehicles coordinate themselves through bridge_enter.
*/

void* carnage_mode()
{
    printf("[MODE] Carnage mode active\n");

    return NULL;
}


/* ============================= */
/* ===== SEMAPHORE MODE ======== */
/* ============================= */

void* semaphore_mode(void *arg)
{
    ModeArgs *args = (ModeArgs*)arg;
    Bridge *b = args->bridge;

    int east_time = args->config->east.green_time;
    int west_time = args->config->west.green_time;

    while (1)
    {
        /* ================= EAST ================= */
        printf("[SEMAPHORE] GREEN EAST (%d sec)\n", east_time);

        bridge_set_direction(b, EAST);

        int t = 0;
        while (t < east_time)
        {
            sleep(1);
            t++;

            /* PRIORIDAD AMBULANCIAS */
            if (bridge_get_ambulances_waiting(b, WEST) > 0)
                break;
        }

        /* Esperar a que el puente quede vacío */
        bridge_wait_until_empty(b);


        /* ================= WEST ================= */
        printf("[SEMAPHORE] GREEN WEST (%d sec)\n", west_time);

        bridge_set_direction(b, WEST);

        t = 0;
        while (t < west_time)
        {
            sleep(1);
            t++;

            /* PRIORIDAD AMBULANCIAS */
            if (bridge_get_ambulances_waiting(b, EAST) > 0)
                break;
        }

        bridge_wait_until_empty(b);

    }

    return NULL;
}


/* ============================= */
/* ===== OFFICER MODE ========== */
/* ============================= */

void* officer_mode(void *arg)
{
    ModeArgs *args = (ModeArgs*)arg;
    Bridge *b = args->bridge;

    int k_east = args->config->east.k_value;
    int k_west = args->config->west.k_value;

    while (1)
    {
        /* ================= EAST ================= */
        bridge_set_direction(b, EAST);
        printf("[OFFICER] Allowing EAST (%d cars)\n", k_east);

        int passed = 0;

        while (passed < k_east)
        {
            sleep(1);

            /* PRIORIDAD AMBULANCIAS */
            if (bridge_get_ambulances_waiting(b, WEST) > 0)
                break;

            /* contar carros que pasan */
            int current = bridge_get_passed_count(b, EAST);

            if (current > passed)
                passed = current;
        }

        bridge_reset_passed_count(b, EAST);

        bridge_wait_until_empty(b);


        /* ================= WEST ================= */
        bridge_set_direction(b, WEST);
        printf("[OFFICER] Allowing WEST (%d cars)\n", k_west);

        passed = 0;

        while (passed < k_west)
        {
            sleep(1);

            if (bridge_get_ambulances_waiting(b, EAST) > 0)
                break;

            int current = bridge_get_passed_count(b, WEST);

            if (current > passed)
                passed = current;
        }

        bridge_reset_passed_count(b, WEST);

        bridge_wait_until_empty(b);

    }

    return NULL;
}


/* ============================= */
/* ===== MODE CONTROLLER ======= */
/* ============================= */

pthread_t start_mode_controller(const Config *config, Bridge *bridge)
{
    pthread_t thread;

    ModeArgs *args = malloc(sizeof(ModeArgs));
    args->bridge = bridge;
    args->config = config;

    switch (config->mode)
    {
        case MODE_CARNAGE:
            pthread_create(&thread, NULL, carnage_mode, args);
            break;

        case MODE_SEMAPHORE:
            pthread_create(&thread, NULL, semaphore_mode, args);
            break;

        case MODE_OFFICER:
            pthread_create(&thread, NULL, officer_mode, args);
            break;

        default:
            printf("[ERROR] Unknown mode\n");
            exit(EXIT_FAILURE);
    }

    return thread;
}