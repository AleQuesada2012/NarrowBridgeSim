#include <stdio.h>
#include <stdlib.h>
#include "config.h"

int main(int argc, char *argv[]) {

    Config config;

    /* Determinar archivo de configuración */
    const char *config_file = "bridge.config";

    if (argc > 1) {
        config_file = argv[1];
    }

    /* Cargar configuración */
    if (!config_load(config_file, &config)) {
        fprintf(stderr, "Failed to load configuration file.\n");
        return EXIT_FAILURE;
    }

    /* Mostrar resumen básico */
    printf("=====================================\n");
    printf(" Bridge Simulation Configuration\n");
    printf("=====================================\n");

    printf("Bridge length: %d meters\n", config.bridge_length);
    printf("Simulation time: %d seconds\n", config.simulation_time);

    printf("\nMode: ");
    switch(config.mode) {
        case MODE_CARNAGE:
            printf("CARNAGE\n");
            break;
        case MODE_SEMAPHORE:
            printf("SEMAPHORE\n");
            break;
        case MODE_OFFICER:
            printf("OFFICER\n");
            break;
    }

    printf("\n--- EAST SIDE ---\n");
    printf("Arrival mean: %.2f\n", config.east.arrival_mean);
    printf("Speed range: %d - %d\n",
           config.east.speed_min,
           config.east.speed_max);
    printf("Ambulance percentage: %d%%\n",
           config.east.ambulance_percentage);
    printf("Green time: %d\n",
           config.east.green_time);
    printf("K value: %d\n",
           config.east.k_value);

    printf("\n--- WEST SIDE ---\n");
    printf("Arrival mean: %.2f\n", config.west.arrival_mean);
    printf("Speed range: %d - %d\n",
           config.west.speed_min,
           config.west.speed_max);
    printf("Ambulance percentage: %d%%\n",
           config.west.ambulance_percentage);
    printf("Green time: %d\n",
           config.west.green_time);
    printf("K value: %d\n",
           config.west.k_value);

    printf("=====================================\n");

    return EXIT_SUCCESS;
}