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
/* ========================================= */
/* ========INTERNAL STRUCTURES============== */
/* ========================================= */


typedef struct {
    int priority;           // 0 = Ambulance (Highest), 1+ = Normal vehicle
    int seq;                // Ticket number like Lottery
    pthread_cond_t cv;
    int ready;
} WaitNode;


typedef struct {
    WaitNode *data[1000];
    int size;
} WaitHeap;



struct Bridge {
    int length;
    pthread_mutex_t lock;
    pthread_mutex_t *slots;

    WaitHeap east_queue;
    WaitHeap west_queue;

    int cars_on_bridge;
    Direction current_direction;
    int next_seq;

    int waiting_east;
    int waiting_west;
    int ambulances_waiting_east;
    int ambulances_waiting_west;

    int passed_count[2];
};
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