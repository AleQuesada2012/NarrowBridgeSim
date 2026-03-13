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
/* ===== Wait Queue Node   ===== */
/* ============================= */

/*
 * One node per vehicle waiting at a bridge entrance.
 * Allocated on the stack inside bridge_enter() — no heap alloc needed.
 *
 * priority:  0 = ambulance (highest), 1..N = arrival sequence (FIFO).
 * seq:       tie-breaker; lower seq was assigned first.
 * ready:     set to 1 by bridge_leave/controller before signalling,
 *            guards against spurious wakeups.
 */
typedef struct WaitNode {
    pthread_cond_t   cv;
    int              priority;   /* 0 = ambulance, 1+ = normal FIFO order  */
    unsigned long    seq;        /* global arrival counter, unique per node */
    int              ready;      /* 1 once this node is at the head & woken */
    struct WaitNode *parent;
    struct WaitNode *left;
    struct WaitNode *right;
} WaitNode;

/* ============================= */
/* ===== Priority Min-Heap  ==== */
/* ============================= */

/*
 * Binary min-heap embedded in the Bridge struct (one per side).
 * Ordering: lower priority value wins; equal priority resolved by seq.
 *
 * We store pointers into WaitNodes that live on vehicle thread stacks,
 * so we never free the nodes themselves here.
 */
#define WAIT_HEAP_MAX 1024

typedef struct {
    WaitNode *data[WAIT_HEAP_MAX];
    int       size;
} WaitHeap;

/* ============================= */
/* ======= Opaque Bridge ======= */
/* ============================= */

typedef struct {
    int length;
    pthread_mutex_t *slots;

    pthread_mutex_t lock;

    int cars_on_bridge;

    int waiting_east;
    int waiting_west;

    int ambulances_waiting_east;
    int ambulances_waiting_west;

    Direction current_direction;

    /* Per-side priority queues — replace the two shared cond vars */
    WaitHeap east_queue;
    WaitHeap west_queue;

    /* Global sequence counter — incremented under bridge->lock */
    unsigned long next_seq;

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
int  bridge_get_length(Bridge *bridge);

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