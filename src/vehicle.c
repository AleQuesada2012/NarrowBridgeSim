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

    vehicle->id = id;
    vehicle->direction = direction;
    vehicle->is_ambulance = is_ambulance;
    vehicle->speed = speed;
    vehicle->bridge = bridge;

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

    BridgeVehicleInfo info = {
        v->id,
        v->direction,
        v->is_ambulance
    };

    double speed_mps = v->speed / 3.6;
    double meter_time = 1.0 / speed_mps;

    bridge_enter(v->bridge, &info);

    //printf("[VEHICLE %d] Entered bridge\n", v->id);

    for (int i = 0; i < bridge_get_length(v->bridge) - 1; i++)
    {
        usleep(meter_time * 1e6);

        bridge_advance(v->bridge, i);
    }

    usleep(meter_time * 1e6);

    //printf("[VEHICLE %d] Exiting bridge\n", v->id);

    bridge_leave(v->bridge, &info);

    free(v);

    return NULL;
}