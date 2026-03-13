#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "bridge.h"

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
    int next_seq;                   // ticket dispenser


    int waiting_east;
    int waiting_west;
    int ambulances_waiting_east;
    int ambulances_waiting_west;
};

/* ========================================= */
/* ==============PRIORITY QUEUE============= */
/* ========================================= */

/* Compares two nodes. Returns 1 if 'a' should go before 'b' */
static inline int heap_before(const WaitNode *a, const WaitNode *b) {
    if (a->priority != b->priority)
        return a->priority < b->priority;
    return a->seq < b->seq;
}


static void heap_push(WaitHeap *h, WaitNode *node) {
    int i = h->size++;
    h->data[i] = node;

    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap_before(h->data[i], h->data[parent])) {
            WaitNode *tmp    = h->data[i];
            h->data[i]       = h->data[parent];
            h->data[parent]  = tmp;
            i = parent;
        } else {
            break;
        }
    }
}

/* Returns the first node in the queue without removing it */
static WaitNode *heap_peek(const WaitHeap *h) {
    return (h->size > 0) ? h->data[0] : NULL;
}

/* Removes the first node from the queue and re-sorts it */
static void heap_pop(WaitHeap *h) {
    if (h->size == 0) return;

    h->data[0] = h->data[--h->size];


    int i = 0;
    for (;;) {
        int left  = 2 * i + 1;
        int right = 2 * i + 2;
        int best  = i;

        if (left  < h->size && heap_before(h->data[left],  h->data[best])) best = left;
        if (right < h->size && heap_before(h->data[right], h->data[best])) best = right;

        if (best == i) break;

        WaitNode *tmp   = h->data[i];
        h->data[i]      = h->data[best];
        h->data[best]   = tmp;
        i = best;
    }
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

/* ==========================================
 * 4. BRIDGE LIFECYCLE
 * ========================================== */

Bridge *bridge_create(const Config *config) {
    Bridge *b = malloc(sizeof(Bridge));

    b->length = config->bridge_length;

    // Allocate and initialize physical slots (1 mutex per meter)
    b->slots = malloc(sizeof(pthread_mutex_t) * b->length);
    for (int i = 0; i < b->length; i++)
        pthread_mutex_init(&b->slots[i], NULL);

    pthread_mutex_init(&b->lock, NULL);

    b->cars_on_bridge          = 0;
    b->waiting_east            = 0;
    b->waiting_west            = 0;
    b->ambulances_waiting_east = 0;
    b->ambulances_waiting_west = 0;
    b->current_direction       = EAST;
    b->next_seq                = 1;

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

void bridge_enter(Bridge *b, BridgeVehicleInfo *info) {
    WaitNode node;
    pthread_cond_init(&node.cv, NULL);
    node.ready = 0;

    pthread_mutex_lock(&b->lock);

    /* Take a ticket and assign priority */
    node.seq = b->next_seq++;
    node.priority = info->is_ambulance ? 0 : (int)node.seq;

    printf("[VEHICLE %d] Arrived at bridge from %s%s\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           info->is_ambulance ? " [AMBULANCE]" : "");

    if (info->direction == EAST) {
        b->waiting_east++;
        if (info->is_ambulance) b->ambulances_waiting_east++;
    } else {
        b->waiting_west++;
        if (info->is_ambulance) b->ambulances_waiting_west++;
    }

    /* Line up in the corresponding prority queue */
    WaitHeap *my_queue = (info->direction == EAST) ? &b->east_queue : &b->west_queue;
    heap_push(my_queue, &node);

    /* If the bridge is free and we are at the front, we pass */
    int opp_amb = (info->direction == EAST) ? b->ambulances_waiting_west : b->ambulances_waiting_east;
    int can_enter = (b->cars_on_bridge == 0 || b->current_direction == info->direction) &&
                    (info->is_ambulance || opp_amb == 0);

    if (can_enter && heap_peek(my_queue) == &node) {
        wake_head(my_queue);
    }

    for (;;) {
        opp_amb = (info->direction == EAST) ? b->ambulances_waiting_west : b->ambulances_waiting_east;

        int blocked_by_direction = (b->cars_on_bridge > 0 && b->current_direction != info->direction);
        int blocked_by_opp_amb = (!info->is_ambulance && opp_amb > 0 && b->current_direction == info->direction);
        int not_my_turn = (heap_peek(my_queue) != &node) || !node.ready;

        if (!blocked_by_direction && !blocked_by_opp_amb && !not_my_turn)
            break;

        pthread_cond_wait(&node.cv, &b->lock);
    }

    /* Enter the bridge */
    heap_pop(my_queue);

    if (info->direction == EAST) {
        b->waiting_east--;
        if (info->is_ambulance) b->ambulances_waiting_east--;
    } else {
        b->waiting_west--;
        if (info->is_ambulance) b->ambulances_waiting_west--;
    }

    b->cars_on_bridge++;
    b->current_direction = info->direction;

    printf("[BRIDGE] Vehicle %d entered from %s. Cars on bridge: %d\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           b->cars_on_bridge);

    /* Wake the next vehicle in line if they can also pipeline onto the bridge */
    opp_amb = (info->direction == EAST) ? b->ambulances_waiting_west : b->ambulances_waiting_east;
    if (opp_amb == 0 && heap_peek(my_queue) != NULL) {
        wake_head(my_queue);
    }

    pthread_mutex_unlock(&b->lock);

    /* P(R) */
    pthread_mutex_lock(&b->slots[0]);
    pthread_cond_destroy(&node.cv);
}

void bridge_advance(Bridge *b, int position) {
    /* P(R+1), V(R) */
    pthread_mutex_lock(&b->slots[position + 1]);
    pthread_mutex_unlock(&b->slots[position]);
}

void bridge_leave(Bridge *b, BridgeVehicleInfo *info) {
    /* V(len-1) */
    pthread_mutex_unlock(&b->slots[b->length - 1]);

    pthread_mutex_lock(&b->lock);

    b->cars_on_bridge--;

    printf("[BRIDGE] Vehicle %d exited going %s. Cars remaining: %d\n",
           info->id,
           info->direction == EAST ? "EAST" : "WEST",
           b->cars_on_bridge);

    /* If the bridge is empty, decide who goes next */
    if (b->cars_on_bridge == 0) {

        if (b->ambulances_waiting_east > 0) {
            b->current_direction = EAST;
            printf("[BRIDGE] PRIORITY: ambulance waiting EAST\n");
            wake_head(&b->east_queue);

        } else if (b->ambulances_waiting_west > 0) {
            b->current_direction = WEST;
            printf("[BRIDGE] PRIORITY: ambulance waiting WEST\n");
            wake_head(&b->west_queue);

        } else if (b->current_direction == EAST) {
            if (b->waiting_west > 0) {
                b->current_direction = WEST;
                printf("[BRIDGE] Switching direction to WEST\n");
                wake_head(&b->west_queue);
            } else {
                wake_head(&b->east_queue);
            }
        } else {
            if (b->waiting_east > 0) {
                b->current_direction = EAST;
                printf("[BRIDGE] Switching direction to EAST\n");
                wake_head(&b->east_queue);
            } else {
                wake_head(&b->west_queue);
            }
        }
    }

    pthread_mutex_unlock(&b->lock);
}

/* ========================================== */
/* ============UTILITY FUNCTIONS============= */
/* ========================================== */

int bridge_get_length(Bridge *bridge) {
    return bridge ? bridge->length : -1;
}