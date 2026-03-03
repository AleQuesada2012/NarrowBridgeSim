#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bridge.h"

/* ============================= */
/* ===== Internal Structure ==== */
/* ============================= */

struct Bridge {
    int bridge_length;
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

    printf("[BRIDGE] Created (length: %d meters)\n",
           bridge->bridge_length);

    return bridge;
}

void bridge_destroy(Bridge *bridge) {
    if (!bridge)
        return;

    printf("[BRIDGE] Destroyed\n");
    free(bridge);
}

/* ============================= */
/* ===== Synchronization  ====== */
/* ============================= */

/* Dummy implementation */
void bridge_arrive(Bridge *bridge, BridgeVehicleInfo *info) {
    (void)bridge;

    printf("[BRIDGE] Vehicle %d arrived (%s)%s\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "");
}

/* Dummy implementation */
void bridge_exit(Bridge *bridge, BridgeVehicleInfo *info) {
    (void)bridge;

    printf("[BRIDGE] Vehicle %d exited bridge\n",
           info->id);
}