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

typedef struct {
    int length;
    pthread_mutex_t *slots;

    pthread_mutex_t lock;

    pthread_cond_t east_cv;
    pthread_cond_t west_cv;

    int cars_on_bridge;

    int waiting_east;
    int waiting_west;

    int ambulances_waiting_east;
    int ambulances_waiting_west;

    Direction current_direction;

} Bridge;


/* ============================= */
/* ===== Initialization ======== */
/* ============================= */

Bridge* bridge_create(const Config *config);
void bridge_destroy(Bridge *bridge);

/* ============================= */
/* ===== Vehicle Interface ===== */
/* ============================= */

void bridge_enter(Bridge *b, BridgeVehicleInfo *info);
void bridge_leave(Bridge *b, BridgeVehicleInfo *info);
void bridge_advance(Bridge *b, int position);
// void bridge_advance(Bridge *b, BridgeVehicleInfo *info, int position); // debugging
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