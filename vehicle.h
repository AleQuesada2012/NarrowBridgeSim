#ifndef VEHICLE_H
#define VEHICLE_H

#include "bridge.h"

/* ============================= */
/* ===== Vehicle Structure ===== */
/* ============================= */

typedef struct {
    int id;
    Direction direction;
    int is_ambulance;
    double speed;       // km/h
    Bridge *bridge;     // puente al que pertenece
} Vehicle;

/* ============================= */
/* ===== Vehicle Lifecycle ===== */
/* ============================= */

/* Creates a vehicle structure */
Vehicle* vehicle_create(
    int id,
    Direction direction,
    int is_ambulance,
    double speed,
    Bridge *bridge
);

/* Frees memory */
void vehicle_destroy(Vehicle *vehicle);

/* Thread routine */
void* vehicle_thread(void *arg);

#endif