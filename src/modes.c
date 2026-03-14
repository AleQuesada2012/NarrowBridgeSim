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

    Bridge *bridge = args->bridge;
    const Config *config = args->config;

    int east_time = config->east.green_time;
    int west_time = config->west.green_time;

    printf("[MODE] Semaphore mode active\n");

    time_t start = time(NULL);

    while (time(NULL) - start < config->simulation_time)
    {
        /* GREEN EAST */
        printf("[SEMAPHORE] GREEN EAST (%d sec)\n", east_time);

        bridge_set_direction(bridge, EAST);

        sleep(east_time);

        /* Wait until bridge empty before switching */
        while (bridge_get_cars_on_bridge(bridge) > 0)
            usleep(100000);


        /* GREEN WEST */
        printf("[SEMAPHORE] GREEN WEST (%d sec)\n", west_time);

        bridge_set_direction(bridge, WEST);

        sleep(west_time);

        while (bridge_get_cars_on_bridge(bridge) > 0)
            usleep(100000);
    }

    return NULL;
}


/* ============================= */
/* ===== OFFICER MODE ========== */
/* ============================= */

// En modes.c

// En modes.c - Versión corregida del modo Officer

void* officer_mode(void *arg)
{
    ModeArgs *args = (ModeArgs*)arg;

    Bridge *bridge = args->bridge;
    const Config *config = args->config;

    int east_k = config->east.k_value;
    int west_k = config->west.k_value;

    printf("[MODE] Officer mode active\n");
    printf("[OFFICER] East K=%d, West K=%d\n", east_k, west_k);

    time_t start = time(NULL);
    Direction current_dir;
    int vehicles_passed_in_phase = 0;
    int target_k;

    while (time(NULL) - start < config->simulation_time)
    {
        // --- Decidir qué sentido activar ---
        // Prioridad 1: ¿Hay ambulancias esperando?
        if (bridge_get_ambulances_waiting(bridge, EAST) > 0) {
            current_dir = EAST;
            printf("[OFFICER] 🚑 PRIORITY: Ambulances waiting EAST\n");
        } else if (bridge_get_ambulances_waiting(bridge, WEST) > 0) {
            current_dir = WEST;
            printf("[OFFICER] 🚑 PRIORITY: Ambulances waiting WEST\n");
        }
        // Prioridad 2: Sentido con vehículos esperando (alternamos justamente)
        else if (bridge_get_waiting(bridge, EAST) > 0 && bridge_get_waiting(bridge, WEST) == 0) {
            current_dir = EAST;
            printf("[OFFICER] Only EAST has waiting vehicles\n");
        } else if (bridge_get_waiting(bridge, WEST) > 0 && bridge_get_waiting(bridge, EAST) == 0) {
            current_dir = WEST;
            printf("[OFFICER] Only WEST has waiting vehicles\n");
        } else if (bridge_get_waiting(bridge, EAST) > 0 && bridge_get_waiting(bridge, WEST) > 0) {
            // Ambos lados tienen vehículos - alternamos basado en el último sentido
            Direction last_dir = bridge_get_direction(bridge);
            current_dir = (last_dir == EAST) ? WEST : EAST;
            printf("[OFFICER] Both sides waiting, alternating to %s\n", 
                   current_dir == EAST ? "EAST" : "WEST");
        } else {
            // No hay nadie esperando, damos un respiro
            printf("[OFFICER] No vehicles waiting, sleeping...\n");
            usleep(500000);
            continue;
        }

        // Establecer la dirección y preparar contadores
        bridge_set_direction(bridge, current_dir);
        target_k = (current_dir == EAST) ? east_k : west_k;
        vehicles_passed_in_phase = 0;
        
        printf("[OFFICER] 🔵 ALLOWING %s for up to %d vehicles\n", 
               current_dir == EAST ? "EAST" : "WEST", target_k);
        
        // Reiniciamos el contador de pasados para este sentido
        bridge_reset_passed_count(bridge, current_dir);

        // --- Permitir el paso de vehículos ---
        while (vehicles_passed_in_phase < target_k) {
            // Verificar tiempo de simulación
            if (time(NULL) - start >= config->simulation_time) {
                printf("[OFFICER] Simulation time ending, waiting for bridge to empty\n");
                // Esperamos a que el puente se vacíe antes de salir
                while (bridge_get_cars_on_bridge(bridge) > 0) {
                    usleep(100000);
                }
                return NULL;
            }

            int current_passed = bridge_get_passed_count(bridge, current_dir);
            
            // Si aumentó el contador, actualizamos
            if (current_passed > vehicles_passed_in_phase) {
                vehicles_passed_in_phase = current_passed;
                printf("[OFFICER] ✅ %d/%d vehicles passed in %s\n", 
                       vehicles_passed_in_phase, target_k,
                       current_dir == EAST ? "EAST" : "WEST");
            }

            // --- VERIFICAR CONDICIONES PARA CAMBIO ANTICIPADO ---
            
            // Condición 1: No hay más vehículos esperando en este sentido Y el puente está vacío
            if (bridge_get_waiting(bridge, current_dir) == 0 && 
                bridge_get_cars_on_bridge(bridge) == 0) {
                printf("[OFFICER] ⏭️  No more vehicles waiting in %s. Switching early (only %d passed)\n",
                       current_dir == EAST ? "EAST" : "WEST", vehicles_passed_in_phase);
                break;
            }
            
            // Condición 2: Hay ambulancias esperando en el otro sentido (prioridad)
            if (current_dir == EAST && bridge_get_ambulances_waiting(bridge, WEST) > 0) {
                printf("[OFFICER] 🚑 Ambulance waiting WEST! Finishing EAST phase early\n");
                // Esperamos a que se vacíe el puente y cambiamos
                while (bridge_get_cars_on_bridge(bridge) > 0) {
                    usleep(100000);
                }
                break;
            }
            if (current_dir == WEST && bridge_get_ambulances_waiting(bridge, EAST) > 0) {
                printf("[OFFICER] 🚑 Ambulance waiting EAST! Finishing WEST phase early\n");
                while (bridge_get_cars_on_bridge(bridge) > 0) {
                    usleep(100000);
                }
                break;
            }

            usleep(100000); // 100ms de pausa para no saturar CPU
        }

        // Esperar a que el puente esté vacío antes de cambiar de sentido
        printf("[OFFICER] ⏳ Waiting for bridge to empty...\n");
        while (bridge_get_cars_on_bridge(bridge) > 0) {
            if (time(NULL) - start >= config->simulation_time) {
                return NULL;
            }
            usleep(100000);
        }
        
        printf("[OFFICER] Bridge is empty. ");

        // Pequeña pausa para que el oficial "observe"
        usleep(300000);
    }

    printf("[OFFICER] Mode finished\n");
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