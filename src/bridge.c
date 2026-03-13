#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "bridge.h"

/* ========================================= */
/* ==============PRIORITY QUEUE============= */
/* ========================================= */

static void heap_push(WaitHeap *h, WaitNode *node) {
    int insert_idx = h->size; // go to the back of the line

    if (node->priority == 0) {
        // It's an ambulance
        insert_idx = 0;
        while (insert_idx < h->size && h->data[insert_idx]->priority == 0) {
            insert_idx++; // Respect other ambulances that arrived first
        }
    }

    // Shift everyone else one step back to make room
    for (int i = h->size; i > insert_idx; i--) {
        h->data[i] = h->data[i - 1];
    }

    // Insert the new car in its spot
    h->data[insert_idx] = node;
    h->size++;
}

/* Looks at the first car in line without removing it */
static WaitNode *heap_peek(const WaitHeap *h) {
    return (h->size > 0) ? h->data[0] : NULL;
}

/* Removes the first car and everyone steps forward */
static void heap_pop(WaitHeap *h) {
    if (h->size == 0) return;

    for (int i = 0; i < h->size - 1; i++) {
        h->data[i] = h->data[i + 1];
    }

    h->size--;
}

/* ========================================= */
/* ================WAKE HEAD================ */
/* ========================================= */


static void wake_head(WaitHeap *h) {
    WaitNode *head = heap_peek(h);
    if (!head) return;
    head->ready = 1;
    pthread_cond_signal(&head->cv);
}

/* ========================================== */
/* ============BRIDGE LIFECYCLE============== */
/* ========================================== */

Bridge *bridge_create(const Config *config) {
    Bridge *b = malloc(sizeof(Bridge));

    b->length = config->bridge_length;

    b->slots = malloc(sizeof(pthread_mutex_t) * b->length);
    for (int i = 0; i < b->length; i++)
        pthread_mutex_init(&b->slots[i], NULL);

    pthread_mutex_init(&b->lock, NULL);

    b->cars_on_bridge          = 0;
    b->waiting_east            = 0;
    b->waiting_west            = 0;
    b->ambulances_waiting_east = 0;
    b->ambulances_waiting_west = 0;
    b->current_direction        = EAST;
    b->next_seq                = 1;
    b->passed_count[EAST]      = 0;
    b->passed_count[WEST]      = 0;

    b->east_queue.size = 0;
    b->west_queue.size = 0;

    printf("[BRIDGE] Created with length: %d meters.\n", b->length);
    return b;
}

void bridge_destroy(Bridge *b) {
    for (int i = 0; i < b->length; i++)
        pthread_mutex_destroy(&b->slots[i]);

    free(b->slots);
    pthread_mutex_destroy(&b->lock);

    printf("=== Bridge destroyed. ===\n");
    free(b);
}

/* ========================================== */
/* =========SYNCHRONIZATION LOGIC============ */
/* ========================================== */

void bridge_enter(Bridge *bridge, BridgeVehicleInfo *info) {
    Direction my_dir = info->direction;
    WaitHeap *my_queue = (my_dir == EAST) ? &bridge->east_queue : &bridge->west_queue;
    WaitNode node;
    pthread_cond_init(&node.cv, NULL);
    node.ready = 0;

    pthread_mutex_lock(&bridge->lock);

    /* 1. Update waiting counters */
    if (my_dir == EAST) {
        bridge->waiting_east++;
        if (info->is_ambulance) bridge->ambulances_waiting_east++;
    } else {
        bridge->waiting_west++;
        if (info->is_ambulance) bridge->ambulances_waiting_west++;
    }

    /* 2. Take a ticket and priority */
    node.seq = bridge->next_seq++;
    node.priority = info->is_ambulance ? 0 : (int)node.seq;

    /* 3. Line up in the queue */
    heap_push(my_queue, &node);

    printf("[VEHICLE %d] Arrived at bridge from %s%s\n",
           info->id,
           my_dir == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "");

    /* 4. Do I need to yield to an opposite ambulance? */
    int opp_amb = (my_dir == EAST) ? bridge->ambulances_waiting_west : bridge->ambulances_waiting_east;
    int yield_to_ambulance = (!info->is_ambulance && opp_amb > 0);
    int safe_direction = (bridge->cars_on_bridge == 0 || bridge->current_direction == my_dir);


    if (safe_direction && !yield_to_ambulance && heap_peek(my_queue) == &node) {
        node.ready = 1;
    }

    while (1) {
        opp_amb = (my_dir == EAST) ? bridge->ambulances_waiting_west : bridge->ambulances_waiting_east;
        yield_to_ambulance = (!info->is_ambulance && opp_amb > 0);
        safe_direction = (bridge->cars_on_bridge == 0 || bridge->current_direction == my_dir);

        int is_my_turn = (heap_peek(my_queue) == &node) && node.ready;

        if (safe_direction && !yield_to_ambulance && is_my_turn) {
            break; // Letss gooo!
        }

        pthread_cond_wait(&node.cv, &bridge->lock);
    }

    /* 5. Entering the bridge */
    heap_pop(my_queue);

    if (my_dir == EAST) {
        bridge->waiting_east--;
        if (info->is_ambulance) bridge->ambulances_waiting_east--;
    } else {
        bridge->waiting_west--;
        if (info->is_ambulance) bridge->ambulances_waiting_west--;
    }

    bridge->cars_on_bridge++;
    bridge->current_direction = my_dir;
    bridge->passed_count[my_dir]++;

    printf("[BRIDGE] Vehicle %d entered from %s. Cars on bridge: %d\n",
           info->id,
           my_dir == EAST ? "EAST" : "WEST",
           bridge->cars_on_bridge);

    /* Wake the next car in my line if its safe */
    opp_amb = (my_dir == EAST) ? bridge->ambulances_waiting_west : bridge->ambulances_waiting_east;
    if (heap_peek(my_queue) != NULL) {
        WaitNode *next_in_line = heap_peek(my_queue);
        int next_is_amb = (next_in_line->priority == 0);

        if (next_is_amb || opp_amb == 0) {
            next_in_line->ready = 1;
            pthread_cond_signal(&next_in_line->cv);
        }
    }

    pthread_mutex_unlock(&bridge->lock);

    /* 6. Lock physical meter */
    pthread_mutex_lock(&bridge->slots[0]);
    pthread_cond_destroy(&node.cv);
}

void bridge_advance(Bridge *b, int position) {
    pthread_mutex_lock(&b->slots[position + 1]);
    pthread_mutex_unlock(&b->slots[position]);
}

void bridge_leave(Bridge *bridge, BridgeVehicleInfo *info) {
    Direction my_dir = info->direction;
    Direction opp_dir = (my_dir == EAST) ? WEST : EAST;

    pthread_mutex_unlock(&bridge->slots[bridge->length - 1]);

    pthread_mutex_lock(&bridge->lock);
    bridge->cars_on_bridge--;

    printf("[BRIDGE] Vehicle %d exited going %s. Cars remaining: %d\n",
           info->id,
           my_dir == EAST ? "EAST" : "WEST",
           bridge->cars_on_bridge);

    if (bridge->cars_on_bridge == 0) {
        bridge->current_direction = NONE;

        WaitHeap *opp_queue = (opp_dir == EAST) ? &bridge->east_queue : &bridge->west_queue;
        WaitHeap *same_queue = (my_dir == EAST) ? &bridge->east_queue : &bridge->west_queue;

        WaitNode *opp_head = heap_peek(opp_queue);
        WaitNode *same_head = heap_peek(same_queue);

        if (opp_head && opp_head->priority == 0) {
            printf("[BRIDGE] PRIORITY: ambulance waiting %s\n", opp_dir == EAST ? "EAST" : "WEST");
            wake_head(opp_queue);
        }
        else if (same_head && same_head->priority == 0) {
            printf("[BRIDGE] PRIORITY: ambulance waiting %s\n", my_dir == EAST ? "EAST" : "WEST");
            wake_head(same_queue);
        }
        else if (opp_head) {
            printf("[BRIDGE] Switching direction to %s\n", opp_dir == EAST ? "EAST" : "WEST");
            wake_head(opp_queue);
        }
        else if (same_head) {
            wake_head(same_queue);
        }
    }

    pthread_mutex_unlock(&bridge->lock);
}

/* ========================================== */
/* ============UTILITY FUNCTIONS============= */
/* ========================================== */

int bridge_get_length(Bridge *bridge) {
    return bridge ? bridge->length : -1;
}