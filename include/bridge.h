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
/* ===== Vehicle Interaction ===== */
/* ============================= */

void bridge_enter(Bridge *bridge, BridgeVehicleInfo *info);
void bridge_advance(Bridge *bridge, int position);
void bridge_leave(Bridge *bridge, BridgeVehicleInfo *info);

int bridge_get_length(Bridge *bridge);
#endif