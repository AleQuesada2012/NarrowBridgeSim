#ifndef BRIDGE_H
#define BRIDGE_H

#include <pthread.h>
#include "config.h"

/* ============================= */
/* ======== Enumerations ======= */
/* ============================= */

typedef enum {
    EAST = 0,
    WEST = 1,
    NONE = 2 // used at the start or when empty
} Direction;

/* ============================= */
/* ======= Vehicle Struct ====== */
/* ============================= */

typedef struct {
    int id;
    Direction direction;
    int is_ambulance;
} BridgeVehicleInfo;

/* ============================= */
/* ======= Opaque Bridge ======= */
/* ============================= */

typedef struct Bridge Bridge;

/* ============================= */
/* ===== Initialization ======== */
/* ============================= */

Bridge* bridge_create(const Config *config);
void bridge_destroy(Bridge *bridge);

/* ============================= */
/* ===== Vehicle Interface ===== */
/* ============================= */

void bridge_arrive(Bridge *bridge, BridgeVehicleInfo *info);
void bridge_exit(Bridge *bridge, BridgeVehicleInfo *info);
int bridge_get_length(Bridge *bridge);

/* ============================= */
/* ===== Controller API ======== */
/* ============================= */

/* Force direction change (used by SEMAPHORE/OFFICER) */
void bridge_set_direction(Bridge *bridge, Direction dir);

/* Get current allowed direction */
Direction bridge_get_direction(Bridge *bridge);

/* ============================= */
/* ===== Monitoring API ======== */
/* ============================= */

int bridge_get_waiting(Bridge *bridge, Direction dir);
int bridge_get_ambulances_waiting(Bridge *bridge, Direction dir);
int bridge_get_cars_on_bridge(Bridge *bridge);
int bridge_get_passed_count(Bridge *bridge, Direction dir);
void bridge_reset_passed_count(Bridge *bridge, Direction dir);

#endif