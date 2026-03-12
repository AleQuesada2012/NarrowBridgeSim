#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bridge.h"

/* ============================= */
/* ===== Internal Structure ==== */
/* ============================= */

//TODO: implement "max cars on bridge" logic

/* ============================= */
/* ===== Bridge Lifecycle  ===== */
/* ============================= */

Bridge *bridge_create(const Config *config)
{
    Bridge *b = malloc(sizeof(Bridge));

    b->length = config->bridge_length;

    b->slots = malloc(sizeof(pthread_mutex_t) * b->length);

    for (int i = 0; i < b->length; i++)
        pthread_mutex_init(&b->slots[i], NULL);

    pthread_mutex_init(&b->lock, NULL);

    pthread_cond_init(&b->east_cv, NULL);
    pthread_cond_init(&b->west_cv, NULL);

    b->cars_on_bridge = 0;
    b->waiting_east = 0;
    b->waiting_west = 0;
    b->ambulances_waiting_east = 0;
    b->ambulances_waiting_west = 0;

    b->current_direction = EAST;
    printf("Bridge created with length: %d meters.\n", b->length);

    return b;
}

void bridge_destroy(Bridge *b)
{
    for (int i = 0; i < b->length; i++)
        pthread_mutex_destroy(&b->slots[i]);

    free(b->slots);

    pthread_mutex_destroy(&b->lock);

    pthread_cond_destroy(&b->east_cv);
    pthread_cond_destroy(&b->west_cv);
    printf("=== Bridge destroyed.===\n");
    free(b);
}

/* ============================= */
/* ===== Synchronization  ====== */
/* ============================= */

void bridge_enter(Bridge *b, BridgeVehicleInfo *info)
{

    // TODO: Handle ambulances
    pthread_mutex_lock(&b->lock);

    printf("[VEHICLE %d] Arrived at bridge from %s\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST");

    if (info->direction == EAST)
    {
        b->waiting_east++;
        if (info->is_ambulance)
            b->ambulances_waiting_east++;
    }
    else
    {
        b->waiting_west++;
        if (info->is_ambulance)
            b->ambulances_waiting_west++;
    }

    int opposite_ambulance_waiting =
        (info->direction == EAST)
            ? b->ambulances_waiting_west
            : b->ambulances_waiting_east;

    while (
        (b->cars_on_bridge > 0 && b->current_direction != info->direction) ||
        /* Ambulance priority rule */
        (!info->is_ambulance &&
         opposite_ambulance_waiting > 0 &&
         b->current_direction == info->direction))
    {
        printf("[VEHICLE %d] Waiting for bridge to switch to (%s)\n",
               info->id,
               info->direction == EAST ? "EAST" : "WEST");

        if (info->direction == EAST)
            pthread_cond_wait(&b->east_cv, &b->lock);
        else
            pthread_cond_wait(&b->west_cv, &b->lock);
    }

    if (info->direction == EAST) {
        b->waiting_east--;
        if (info->is_ambulance)
            b->ambulances_waiting_east--;
    }
    else {
        b->waiting_west--;
        if (info->is_ambulance)
            b->ambulances_waiting_west--;
    }

    b->cars_on_bridge++;
    b->current_direction = info->direction;

    printf("[BRIDGE] Vehicle %d entering (%s). Cars on bridge: %d\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           b->cars_on_bridge);

    pthread_mutex_unlock(&b->lock);

    pthread_mutex_lock(&b->slots[0]);
}

void bridge_advance(Bridge *b, int position)
{
    pthread_mutex_lock(&b->slots[position + 1]);
    pthread_mutex_unlock(&b->slots[position]);

    // printf("[VEHICLE %d] Moved to slot %d\n", info->id, position + 1);
}

void bridge_leave(Bridge *b, BridgeVehicleInfo *info)
{
    pthread_mutex_unlock(&b->slots[b->length - 1]);

    pthread_mutex_lock(&b->lock);

    b->cars_on_bridge--;

    printf("[BRIDGE] Vehicle %d exited (%s). Cars remaining: %d\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           b->cars_on_bridge);

    if (b->cars_on_bridge == 0)
    {
        /* Ambulance priority first */

        if (b->ambulances_waiting_east > 0)
        {
            b->current_direction = EAST;
            printf("[BRIDGE] PRIORITY: ambulance waiting EAST\n");
            pthread_cond_broadcast(&b->east_cv);
        }
        else if (b->ambulances_waiting_west > 0)
        {
            b->current_direction = WEST;
            printf("[BRIDGE] PRIORITY: ambulance waiting WEST\n");
            pthread_cond_broadcast(&b->west_cv);
        }

        /* Normal flow otherwise */

        else if (b->current_direction == EAST)
        {
            if (b->waiting_west > 0)
            {
                b->current_direction = WEST;
                printf("[BRIDGE] Switching direction to WEST\n");
                pthread_cond_broadcast(&b->west_cv);
            }
            else
            {
                pthread_cond_broadcast(&b->east_cv);
            }
        }
        else
        {
            if (b->waiting_east > 0)
            {
                b->current_direction = EAST;
                printf("[BRIDGE] Switching direction to EAST\n");
                pthread_cond_broadcast(&b->east_cv);
            }
            else
            {
                pthread_cond_broadcast(&b->west_cv);
            }
        }
    }

    pthread_mutex_unlock(&b->lock);
}

int bridge_get_length(Bridge *bridge)
{
    if (!bridge)
        return -1;

    return bridge->length;
}