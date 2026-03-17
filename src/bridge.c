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

    b->passed_east = 0;
    b->passed_west = 0;

    for (int i = 0; i < b->length; i++)
        pthread_mutex_init(&b->slots[i], NULL);

    pthread_mutex_init(&b->lock, NULL);

    pthread_cond_init(&b->east_cv, NULL);
    pthread_cond_init(&b->west_cv, NULL);
    pthread_cond_init(&b->empty_cv, NULL);

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

    pthread_mutex_lock(&b->lock);

    printf("[VEHICLE %d] Arrived at bridge from %s\n",
        info->id,
        info->direction == EAST ? "EAST" : "WEST");

    /* Registrar espera */
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

    /* Si el puente está vacío, decidir dirección inteligentemente */
    if (b->cars_on_bridge == 0)
    {
        // PRIORIDAD ambulancias
        if (b->ambulances_waiting_east > 0)
            b->current_direction = EAST;
        else if (b->ambulances_waiting_west > 0)
            b->current_direction = WEST;

        // Si no hay ambulancias, pero hay carros en el otro lado
        else if (info->direction == EAST && b->waiting_west > 0)
            b->current_direction = WEST;
        else if (info->direction == WEST && b->waiting_east > 0)
            b->current_direction = EAST;

        // Si no hay nadie más, dejamos que pase el que llegó
        else
            b->current_direction = info->direction;
    }


    while (
        (b->current_direction != info->direction) ||
        (!info->is_ambulance &&
        ((info->direction == EAST ? b->ambulances_waiting_west
                                : b->ambulances_waiting_east) > 0))) //&&
        //b->current_direction == info->direction
    {
        printf("[VEHICLE %d] Waiting for bridge to switch to (%s)\n",
            info->id,
            info->direction == EAST ? "EAST" : "WEST");

        if (info->direction == EAST)
            pthread_cond_wait(&b->east_cv, &b->lock);
        else
            pthread_cond_wait(&b->west_cv, &b->lock);
    }

    /* Quitar de espera */
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

    /* Entrar al puente */
    b->cars_on_bridge++;

    printf("[BRIDGE] Vehicle %d entered from %s. Cars on bridge: %d\n",
        info->id,
        info->direction == EAST ? "EAST" : "WEST",
        b->cars_on_bridge);

    pthread_mutex_unlock(&b->lock);

    /* Primer slot */
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

    if (b->cars_on_bridge == 0)
    {
        /* DESPERTAR a los modos */
        pthread_cond_broadcast(&b->empty_cv);
    }

    printf("[BRIDGE] Vehicle %d exited coming from %s. Cars remaining: %d\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           b->cars_on_bridge);

    if (b->cars_on_bridge == 0)
    {
        /* Ambulance priority first */

        if (b->ambulances_waiting_east > 0)
        {
            b->current_direction = EAST;
            printf("[BRIDGE] PRIORITY switch: ambulance waiting EAST\n");
        }
        else if (b->ambulances_waiting_west > 0)
        {
            b->current_direction = WEST;
            printf("[BRIDGE] PRIORITY switch: ambulance waiting WEST\n");
        }

        /* Normal flow otherwise */

        else if (b->current_direction == EAST)
        {
            if (b->waiting_west > 0)
            {
                b->current_direction = WEST;
                printf("[BRIDGE] Switching direction to WEST\n");
            }
        }
        else
        {
            if (b->waiting_east > 0)
            {
                b->current_direction = EAST;
                printf("[BRIDGE] Switching direction to EAST\n"); 
            }
        }
        // wake up both side and let the while condition decide based on the current direction
        pthread_cond_broadcast(&b->east_cv);
        pthread_cond_broadcast(&b->west_cv);
    }

    pthread_mutex_unlock(&b->lock);

    if (info->direction == EAST)
        b->passed_east++;
    else
        b->passed_west++;
}

int bridge_get_length(Bridge *bridge)
{
    if (!bridge)
        return -1;

    return bridge->length;
}


int bridge_get_passed_count(Bridge *b, Direction dir)
{
    pthread_mutex_lock(&b->lock);

    int value = (dir == EAST) ? b->passed_east : b->passed_west;

    pthread_mutex_unlock(&b->lock);

    return value;
}

void bridge_reset_passed_count(Bridge *b, Direction dir)
{
    pthread_mutex_lock(&b->lock);

    if (dir == EAST)
        b->passed_east = 0;
    else
        b->passed_west = 0;

    pthread_mutex_unlock(&b->lock);
}

void bridge_set_direction(Bridge *bridge, Direction dir)
{
    pthread_mutex_lock(&bridge->lock);

    if (bridge->cars_on_bridge == 0)
    {
        bridge->current_direction = dir;

        printf("[BRIDGE] Direction set to %s\n",
               dir == EAST ? "EAST" : "WEST");

        if (dir == EAST)
            pthread_cond_broadcast(&bridge->east_cv);
        else
            pthread_cond_broadcast(&bridge->west_cv);
    }

    pthread_mutex_unlock(&bridge->lock);
}


Direction bridge_get_direction(Bridge *bridge)
{
    return bridge->current_direction;
}

int bridge_get_cars_on_bridge(Bridge *bridge)
{
    if (!bridge)
        return 0; // o -1 si quieres indicar error

    pthread_mutex_lock(&bridge->lock);
    int cars = bridge->cars_on_bridge;
    pthread_mutex_unlock(&bridge->lock);

    return cars;
}

int bridge_get_waiting(Bridge *bridge, Direction dir)
{
    pthread_mutex_lock(&bridge->lock);
    int waiting = (dir == EAST) ? bridge->waiting_east : bridge->waiting_west;
    pthread_mutex_unlock(&bridge->lock);
    return waiting;
}


int bridge_get_ambulances_waiting(Bridge *bridge, Direction dir)
{
    if (!bridge) return 0;
    
    pthread_mutex_lock(&bridge->lock);
    int waiting = (dir == EAST) ? bridge->ambulances_waiting_east : bridge->ambulances_waiting_west;
    pthread_mutex_unlock(&bridge->lock);
    
    return waiting;
}

void bridge_wait_until_empty(Bridge *b)
{
    pthread_mutex_lock(&b->lock);

    while (b->cars_on_bridge > 0)
    {
        pthread_cond_wait(&b->empty_cv, &b->lock);
    }

    pthread_mutex_unlock(&b->lock);
}
