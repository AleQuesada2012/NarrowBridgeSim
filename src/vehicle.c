#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vehicle.h"

/* ============================= */
/* ===== Vehicle Creation  ===== */
/* ============================= */

Vehicle* vehicle_create(
    int id,
    Direction direction,
    int is_ambulance,
    double speed,
    Bridge *bridge
) {
    Vehicle *vehicle = malloc(sizeof(Vehicle));
    if (!vehicle)
        return NULL;

    vehicle->id          = id;
    vehicle->direction   = direction;
    vehicle->is_ambulance = is_ambulance;
    vehicle->speed       = speed;
    vehicle->bridge      = bridge;

    return vehicle;
}

void vehicle_destroy(Vehicle *vehicle) {
    if (vehicle)
        free(vehicle);
}

/* ============================= */
/* ===== Thread Routine    ===== */
/* ============================= */

void* vehicle_thread(void *arg)
{
    Vehicle *v = arg;

    printf("[VEHICLE %d] Created (%s) speed=%.2f km/h %s\n",
           v->id,
           v->direction == EAST ? "EAST" : "WEST",
           v->speed,
           v->is_ambulance ? "[AMBULANCE]" : "");
    fflush(stdout);

    BridgeVehicleInfo info = {
        v->id,
        v->direction,
        v->is_ambulance
    };

    double speed_mps  = v->speed / 3.6;
    double meter_time = 1.0 / speed_mps;

    bridge_enter(v->bridge, &info);

    /*
     * Structured slot-position log — consumed by the GUI process.
     *
     * Format: [SLOT <id> <slot> <bridge_len> <direction> <is_ambulance>]
     *   slot == -1   → vehicle has fully exited the bridge
     *   slot ==  0   → vehicle just entered (first meter)
     *   slot ==  N   → vehicle is at meter N (0-based)
     *
     * direction is "EAST" or "WEST".
     * All fields are separated by spaces inside the brackets.
     */
    printf("[SLOT %d 0 %d %s %d]\n",
           v->id, v->bridge->length,
           v->direction == EAST ? "EAST" : "WEST",
           v->is_ambulance);
    fflush(stdout);

    for (int i = 0; i < bridge_get_length(v->bridge) - 1; i++)
    {
        usleep((useconds_t)(meter_time * 1e6));
        bridge_advance(v->bridge, i);

        printf("[SLOT %d %d %d %s %d]\n",
               v->id, i + 1, v->bridge->length,
               v->direction == EAST ? "EAST" : "WEST",
               v->is_ambulance);
        fflush(stdout);
    }

    usleep((useconds_t)(meter_time * 1e6));
    bridge_leave(v->bridge, &info);

    /* Slot -1: vehicle fully exited */
    printf("[SLOT %d -1 %d %s %d]\n",
           v->id, v->bridge->length,
           v->direction == EAST ? "EAST" : "WEST",
           v->is_ambulance);
    fflush(stdout);

    free(v);
    return NULL;
}