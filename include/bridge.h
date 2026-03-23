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

typedef enum {
    SAME                = 0,
    OPP                 = 1,
    OFFICERs_DAY_OFF    = 2   /* CARNAGE and SEMAPHORE modes */
} OfficerState;

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

    /* Counters for OFFICER MODE */
    int              current_k_value;       
    int              ambulance_reset;   // 1 if the flow is interrupted by an ambulance
                                        // 0 else
    OfficerState     officer_side[2];
    /*
     * Whose turn it is (EAST or WEST).
     * Written only by officer threads; read by both officer threads.
     * Protected by bridge->lock.
     */
    Direction        officer_turn;
 
    /*
     * officer_cv   — signalled when k changes or ambulance_reset fires.
     *                The active officer thread waits here.
     *
     * officer_done_cv — broadcast when officer_turn changes.
     *                   Both officer threads wait here until it is their turn.
     */
    pthread_cond_t   officer_cv;
    pthread_cond_t   officer_done_cv;
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
void    bridge_set_officer(Bridge *bridge, Direction current_side, int k);
int     bridge_get_current_k(Bridge *bridge);
int     bridge_get_ambulance_reset(Bridge *bridge);
void    decrement_k_and_notify(Bridge *bridge);

#endif