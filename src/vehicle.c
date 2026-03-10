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

void* vehicle_thread(void *arg) {

    Vehicle *v = (Vehicle*) arg;

    if (!v)
        return NULL;

    BridgeVehicleInfo info;
    info.id = v->id;
    info.direction = v->direction;
    info.is_ambulance = v->is_ambulance;

    printf("[VEHICLE %d] Arriving from %s%s\n",
           v->id,
           v->direction == EAST ? "EAST" : "WEST",
           v->is_ambulance ? " [AMBULANCE]" : "");

    /* Notify bridge */
    bridge_arrive(v->bridge, &info);

    printf("[VEHICLE %d] Crossing bridge...\n", v->id);

    /* Simulate crossing time */
    int b_length = bridge_get_length(v->bridge);
    double speed_m_per_sec = (v->speed * 1000) / 3600.0; // unit conversion km/h to m/s
    double crossing_time = b_length / speed_m_per_sec;
    usleep(crossing_time * 1e6); // convert to microseconds

    /* Notify bridge exit */
    bridge_exit(v->bridge, &info);

    printf("[VEHICLE %d] Finished crossing\n", v->id);

    vehicle_destroy(v);

    return NULL;
}