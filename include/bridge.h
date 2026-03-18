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

/* ============================= */
/* ========= FIFO QUEUE ======== */
/* ============================= */

/*
 * One node per waiting vehicle, allocated on the vehicle thread's stack.
 * Vehicles join the tail in strict arrival order — no reordering ever.
 *
 * The bridge inspects head->is_ambulance to apply priority rules without
 * needing any separate "entry slot" structure.
 */
typedef struct FifoNode {
    pthread_cond_t   cv;           /* this vehicle sleeps here            */
    int              is_ambulance; /* cached so the bridge can peek it    */
    int              id;           /* for logging                         */
    struct FifoNode *next;
} FifoNode;

typedef struct {
    FifoNode *head;   /* next vehicle to enter the bridge */
    FifoNode *tail;   /* most recently arrived vehicle    */
    int       size;
} FifoQueue;

/* ============================= */
/* ========== BRIDGE =========== */
/* ============================= */

struct Bridge {
    int              length;
    pthread_mutex_t  lock;
    pthread_mutex_t *slots;        /* per-meter mutex array               */

    FifoQueue        queue[2];     /* one FIFO per side, indexed by Direction */

    int              cars_on_bridge;
    Direction        current_direction;

    /* Counters kept for logging */
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

#endif