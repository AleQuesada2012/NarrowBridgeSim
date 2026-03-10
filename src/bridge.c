#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bridge.h"

/* ============================= */
/* ===== Internal Structure ==== */
/* ============================= */

struct Bridge {
    int bridge_length;
    pthread_mutex_t lock;
    pthread_cond_t cond_east;
    pthread_cond_t cond_west;

    Direction active_direction;
    int cars_on_bridge;

    int waiting[2];
    int ambulances_waiting[2];

    int passed_count[2];
};

/* ============================= */
/* ===== Bridge Lifecycle  ===== */
/* ============================= */

Bridge* bridge_create(const Config *config) {
    if (!config)
        return NULL;

    Bridge *bridge = malloc(sizeof(Bridge));
    if (!bridge)
        return NULL;

    bridge->bridge_length = config->bridge_length;

    pthread_mutex_init(&bridge->lock, NULL);
    pthread_cond_init(&bridge->cond_east, NULL);
    pthread_cond_init(&bridge->cond_west, NULL);

    bridge->active_direction = NONE;
    bridge->cars_on_bridge = 0;

    bridge->waiting[EAST] = 0;
    bridge->waiting[WEST] = 0;
    bridge->ambulances_waiting[EAST] = 0;
    bridge->ambulances_waiting[WEST] = 0;

    bridge->passed_count[EAST] = 0;
    bridge->passed_count[WEST] = 0;

    printf("[BRIDGE] Created (length: %d meters)\n", bridge->bridge_length);

    return bridge;
}

void bridge_destroy(Bridge *bridge) {
    if (!bridge)
        return;

    pthread_mutex_destroy(&bridge->lock);
    pthread_cond_destroy(&bridge->cond_east);
    pthread_cond_destroy(&bridge->cond_west);

    printf("[BRIDGE] Destroyed\n");
    free(bridge);
}

/* ============================= */
/* ===== Synchronization  ====== */
/* ============================= */


void bridge_arrive(Bridge *bridge, BridgeVehicleInfo *info) {
    if (!bridge || !info) return;

    Direction my_dir = info->direction;
    Direction opp_dir = (my_dir == EAST) ? WEST : EAST;


    pthread_mutex_lock(&bridge->lock);

    // register the arrival
    bridge->waiting[my_dir]++;
    if (info->is_ambulance) {
        bridge->ambulances_waiting[my_dir]++;
    }

    printf("[BRIDGE] Vehicle %d (%s)%s arrived and is waiting.\n",
           info->id,
           my_dir == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "");


    while (1) {
        // the bridge is empty or traffic is moving in my direction
        int safe_direction = (bridge->cars_on_bridge == 0 || bridge->active_direction == my_dir);


        // if I am NOT an ambulance, and there is an ambulance waiting on the opposite side, I yield.
        int yield_to_ambulance = (!info->is_ambulance && bridge->ambulances_waiting[opp_dir] > 0);

        if (safe_direction && !yield_to_ambulance) {
            break;
        }

        // if we cannot pass, we wait on the correct condition variable
        if (my_dir == EAST) {
            pthread_cond_wait(&bridge->cond_east, &bridge->lock);
        } else {
            pthread_cond_wait(&bridge->cond_west, &bridge->lock);
        }
    }

    // update bridge state upon entry
    bridge->waiting[my_dir]--;
    if (info->is_ambulance) {
        bridge->ambulances_waiting[my_dir]--;
    }

    bridge->cars_on_bridge++;
    bridge->active_direction = my_dir;
    bridge->passed_count[my_dir]++;

    printf("[BRIDGE] Vehicle %d ENTERED the bridge. (Total crossing: %d)\n",
           info->id, bridge->cars_on_bridge);

    // unlock the mutex
    pthread_mutex_unlock(&bridge->lock);
}

void bridge_exit(Bridge *bridge, BridgeVehicleInfo *info) {
    if (!bridge || !info) return;

    Direction my_dir = info->direction;
    Direction opp_dir = (my_dir == EAST) ? WEST : EAST;

    pthread_mutex_lock(&bridge->lock);

    // register the exit
    bridge->cars_on_bridge--;
    printf("[BRIDGE] Vehicle %d EXITED the bridge. (Remaining: %d)\n",
           info->id, bridge->cars_on_bridge);

    // wake up waiting threads ONLY if the bridge is completely empty
    if (bridge->cars_on_bridge == 0) {

        bridge->active_direction = NONE;

        // decide who to wake up based on priorities:

        // 1- ambulances waiting on the opposite side?
        if (bridge->ambulances_waiting[opp_dir] > 0) {
            printf("[BRIDGE] Empty bridge. Waking up opposite side for AMBULANCE.\n");
            if (opp_dir == EAST) pthread_cond_broadcast(&bridge->cond_east);
            else pthread_cond_broadcast(&bridge->cond_west);
        }
        // 2- ambulances waiting on the same side?
        else if (bridge->ambulances_waiting[my_dir] > 0) {
            printf("[BRIDGE] Empty bridge. Waking up same side for AMBULANCE.\n");
            if (my_dir == EAST) pthread_cond_broadcast(&bridge->cond_east);
            else pthread_cond_broadcast(&bridge->cond_west);
        }
        // 3- are there regular cars waiting on the opposite side?
        else if (bridge->waiting[opp_dir] > 0) {
            printf("[BRIDGE] Empty bridge. Waking up opposite side.\n");
            if (opp_dir == EAST) pthread_cond_broadcast(&bridge->cond_east);
            else pthread_cond_broadcast(&bridge->cond_west);
        }
        // 4- are there regular cars waiting on the same side?
        else if (bridge->waiting[my_dir] > 0) {
            printf("[BRIDGE] Empty bridge. Waking up same side.\n");
            if (my_dir == EAST) pthread_cond_broadcast(&bridge->cond_east);
            else pthread_cond_broadcast(&bridge->cond_west);
        }
    }

    pthread_mutex_unlock(&bridge->lock);
}

int bridge_get_length(Bridge *bridge) {
    if (!bridge)
        return -1;

    return bridge->bridge_length;
}