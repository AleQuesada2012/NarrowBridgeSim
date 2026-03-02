#include "bridge.h"

struct Bridge {

    /* ============================= */
    /* Synchronization */
    /* ============================= */

    pthread_mutex_t mutex;
    pthread_cond_t cond[2];  // cond[EAST], cond[WEST]

    /* ============================= */
    /* Bridge State */
    /* ============================= */

    Direction current_direction;
    int cars_on_bridge;

    /* ============================= */
    /* Waiting Counters */
    /* ============================= */

    int waiting[2];
    int ambulances_waiting[2];

    /* ============================= */
    /* Officer Mode Support */
    /* ============================= */

    int passed_count[2];

    /* ============================= */
    /* Configuration */
    /* ============================= */

    int length;

    /* ============================= */
    /* Control */
    /* ============================= */

    int running;
};