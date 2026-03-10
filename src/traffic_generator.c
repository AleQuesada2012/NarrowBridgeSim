#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>

#include "traffic_generator.h"
#include "vehicle.h"
#include "config.h"

/* ============================= */
/* ===== Global ID Control ===== */
/* ============================= */

static pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;
static int global_vehicle_id = 1;

static int next_vehicle_id() {
    pthread_mutex_lock(&id_mutex);
    int id = global_vehicle_id++;
    pthread_mutex_unlock(&id_mutex);
    return id;
}

/* ============================= */
/* ===== Helper Functions  ===== */
/* ============================= */

static double uniform_random() {
    return (double)rand() / (double)RAND_MAX;
}

static double exponential(double mean) {
    double u = uniform_random();

    if (u < 1e-10)
        u = 1e-10; // purpose: avoiding log(0)

    return -mean * log(u);
}

static int random_ambulance(double probability) {
    return (uniform_random() < probability);
}

static double random_speed(int min, int max) {
    return min + uniform_random() * (max - min);
}

/* ============================= */
/* ===== Generator Arguments ==== */
/* ============================= */

typedef struct {
    const Config *config;
    Bridge *bridge;
    Direction direction;
    time_t start_time;
} GeneratorArgs;

/* ============================= */
/* ===== Generator Thread  ===== */
/* ============================= */

static void* generator_thread(void *arg) {

    GeneratorArgs *args = (GeneratorArgs*) arg;

    const Config *config = args->config;
    Bridge *bridge = args->bridge;
    Direction direction = args->direction;
    time_t start_time = args->start_time;

    const SideConfig *side = (direction == EAST)
                             ? &config->east
                             : &config->west;

    printf("[GENERATOR (%s)] Started\n",
           direction == EAST ? "EAST" : "WEST");

    /* Dynamic array of vehicle threads */
    size_t capacity = 100;
    size_t count = 0; // being size_t, it is printed with %zu instead of %d

    pthread_t *threads = malloc(capacity * sizeof(pthread_t));

    if (!threads) {
        fprintf(stderr, "Failed to allocate thread array.\n");
        return NULL;
    }

    while (difftime(time(NULL), start_time) < config->simulation_time) {

        /* Generate inter-arrival time */
        double wait_time = exponential(side->arrival_mean);

        usleep((useconds_t)(wait_time * 1e6));

        if (difftime(time(NULL), start_time) >= config->simulation_time)
            break;

        /* Generate vehicle attributes */
        int id = next_vehicle_id();
        int is_ambulance = random_ambulance(side->ambulance_percentage);
        double speed = random_speed(side->speed_min, side->speed_max);

        Vehicle *vehicle = vehicle_create(
            id,
            direction,
            is_ambulance,
            speed,
            bridge
        );

        if (!vehicle) {
            fprintf(stderr, "Failed to create vehicle.\n");
            continue;
        }

        // printf("[GENERATOR (%s)] Vehicle %d | Ambulance: %s | Speed: %.2f km/h\n",
        //        direction == EAST ? "EAST" : "WEST",
        //        id,
        //        is_ambulance ? "YES" : "NO",
        //        speed);

        pthread_t tid;

        if (pthread_create(&tid, NULL, vehicle_thread, vehicle) != 0) {
            fprintf(stderr, "Failed to create thread for vehicle %d\n", id);
            free(vehicle);
            continue;
        }

        /* Expand thread array if necessary */
        if (count >= capacity) {
            capacity *= 2;

            pthread_t *new_threads =
                realloc(threads, capacity * sizeof(pthread_t));

            if (!new_threads) {
                fprintf(stderr, "Failed to expand thread array.\n");
                break;
            }

            threads = new_threads;
        }

        threads[count++] = tid;
    }

    printf("[GENERATOR (%s)] Finished generation. Waiting for %zu vehicles...\n",
           direction == EAST ? "EAST" : "WEST",
           count);

    /* Join all vehicle threads created by this generator */
    for (size_t i = 0; i < count; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("[GENERATOR (%s)] All vehicles finished.\n",
           direction == EAST ? "EAST" : "WEST");

    free(threads);

    return NULL;
}

/* ============================= */
/* ===== Traffic Generator ===== */
/* ============================= */

void traffic_generator_start(const Config *config, Bridge *bridge) {

    if (!config || !bridge) {
        fprintf(stderr, "Traffic generator received NULL parameter.\n");
        return;
    }

    srand(time(NULL));

    printf("=== Traffic simulation started ===\n");
    printf("Simulation time: %d seconds\n\n", config->simulation_time);

    time_t start_time = time(NULL);

    pthread_t east_thread;
    pthread_t west_thread;

    GeneratorArgs east_args = {
        config,
        bridge,
        EAST,
        start_time
    };

    GeneratorArgs west_args = {
        config,
        bridge,
        WEST,
        start_time
    };

    pthread_create(&east_thread, NULL, generator_thread, &east_args);
    pthread_create(&west_thread, NULL, generator_thread, &west_args);

    pthread_join(east_thread, NULL);
    pthread_join(west_thread, NULL);

    printf("\n=== Simulation complete ===\n");
}