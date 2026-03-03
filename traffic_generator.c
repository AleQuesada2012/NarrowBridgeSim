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
/* ===== Helper Functions  ===== */
/* ============================= */

static double uniform_random() {
    return (double)rand() / (double)RAND_MAX;
}

static double exponential(double mean) {
    double u = uniform_random();

    if (u < 1e-10)
        u = 1e-10;

    return -mean * log(u);
}

static Direction random_direction() {
    return (rand() % 2 == 0) ? EAST : WEST;
}

static int random_ambulance(double probability) {
    return (uniform_random() < probability);
}

static double random_speed(int min, int max) {
    return min + uniform_random() * (max - min);
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
    int vehicle_id = 0;

    /* Dynamic array of thread IDs */
    size_t capacity = 100;
    size_t count = 0;
    pthread_t *threads = malloc(capacity * sizeof(pthread_t));

    if (!threads) {
        fprintf(stderr, "Failed to allocate thread array.\n");
        return;
    }

    while (difftime(time(NULL), start_time) < config->simulation_time) {

        /* 1️⃣ Choose direction */
        Direction direction = random_direction();

        /* 2️⃣ Select corresponding side config */
        const SideConfig *side = (direction == EAST)
                         ? &config->east
                         : &config->west;

        /* 3️⃣ Generate inter-arrival time */
        double wait_time = exponential(side->arrival_mean);

        usleep((useconds_t)(wait_time * 1e6));

        if (difftime(time(NULL), start_time) >= config->simulation_time)
            break;

        /* 4️⃣ Generate vehicle attributes */
        int is_ambulance = random_ambulance(side->ambulance_percentage);
        double speed = random_speed(side->speed_min, side->speed_max);

        Vehicle *vehicle = vehicle_create(
            vehicle_id++,
            direction,
            is_ambulance,
            speed,
            bridge
        );

        if (!vehicle) {
            fprintf(stderr, "Failed to create vehicle.\n");
            continue;
        }

        printf("[GENERATOR] Vehicle %d | Dir: %s | Ambulance: %s | Speed: %.2f km/h\n",
               vehicle->id,
               direction == EAST ? "EAST" : "WEST",
               is_ambulance ? "YES" : "NO",
               speed);

        pthread_t tid;

        if (pthread_create(&tid, NULL, vehicle_thread, vehicle) != 0) {
            fprintf(stderr, "Failed to create thread for vehicle %d\n", vehicle->id);
            free(vehicle);
            continue;
        }

        /* Resize thread array if needed */
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

    printf("\n=== Traffic generation finished ===\n");
    printf("Waiting for %zu vehicles to finish...\n\n", count);

    /* Join all threads */
    for (size_t i = 0; i < count; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("=== Simulation complete ===\n");

    free(threads);
}