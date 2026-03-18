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
    NONE = 2
} Direction;

typedef enum {
    LIGHT_GREEN = 0,
    LIGHT_RED   = 1,
    LIGHT_OFF   = 2   /* CARNAGE and OFFICER modes */
} LightState;

/* ============================= */
/* ========= FIFO QUEUE ======== */
/* ============================= */

/*
 * One node per waiting vehicle, stack-allocated inside bridge_enter().
 * Vehicles join the tail in strict arrival order — no reordering ever.
 * The bridge inspects head->is_ambulance to apply priority rules.
 */
typedef struct FifoNode {
    pthread_cond_t   cv;
    int              is_ambulance;
    int              id;            /* for logging */
    struct FifoNode *next;
} FifoNode;

typedef struct {
    FifoNode *head;
    FifoNode *tail;
    int       size;
} FifoQueue;

/* ============================= */
/* ========== BRIDGE =========== */
/* ============================= */

struct Bridge {
    int              length;
    pthread_mutex_t  lock;
    pthread_mutex_t *slots;

    Mode             mode;

    /* One FIFO queue per side (index by Direction: EAST=0, WEST=1) */
    FifoQueue        queue[2];

    /* Traffic light state per side — only meaningful in SEMAPHORE mode */
    LightState       light[2];

    int              cars_on_bridge;
    Direction        current_direction;

    /* Counters for logging */
    int              waiting[2];
    int              ambulances_waiting[2];
    int              passed_count[2];
};

/* ============================= */
/* ======= Vehicle Struct ====== */
/* ============================= */

typedef struct {
    int       id;
    Direction direction;
    int       is_ambulance;
} BridgeVehicleInfo;

typedef struct Bridge Bridge;

/* ============================= */
/* ===== Public Interface  ===== */
/* ============================= */

Bridge *bridge_create(const Config *config);
void    bridge_destroy(Bridge *bridge);

void    bridge_enter(Bridge *bridge, BridgeVehicleInfo *info);
void    bridge_advance(Bridge *bridge, int position);
void    bridge_leave(Bridge *bridge, BridgeVehicleInfo *info);

int     bridge_get_length(Bridge *bridge);

/*
 * Called by the semaphore thread to flip the lights.
 * Acquires bridge->lock internally.
 */
void    bridge_set_light(Bridge *bridge, Direction green_side);

#endif