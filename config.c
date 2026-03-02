#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"

#define MAX_LINE 256

/* ========================= */
/* ==== Helper Functions ==== */
/* ========================= */

static void trim(char *str) {
    char *start = str;
    char *end;

    /* Skip leading whitespace */
    while (isspace((unsigned char)*start))
        start++;

    if (*start == 0) {
        *str = '\0';
        return;
    }

    /* Move trimmed string to beginning */
    if (start != str)
        memmove(str, start, strlen(start) + 1);

    /* Remove trailing whitespace */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;

    *(end + 1) = '\0';
}

static int is_empty_or_comment(const char *line) {
    while (*line) {
        if (!isspace((unsigned char)*line))
            break;
        line++;
    }
    return (*line == '#' || *line == '\0');
}

static void set_mode(const char *value, Config *config) {
    if (strcmp(value, "CARNAGE") == 0)
        config->mode = MODE_CARNAGE;
    else if (strcmp(value, "SEMAPHORE") == 0)
        config->mode = MODE_SEMAPHORE;
    else if (strcmp(value, "OFFICER") == 0)
        config->mode = MODE_OFFICER;
    else {
        fprintf(stderr, "Invalid mode: %s\n", value);
        exit(EXIT_FAILURE);
    }
}

/* ========================= */
/* ===== Validation ======== */
/* ========================= */

static void validate_side(const char *side_name, SideConfig *side) {

    if (side->arrival_mean <= 0) {
        fprintf(stderr, "%s arrival_mean must be > 0\n", side_name);
        exit(EXIT_FAILURE);
    }

    if (side->speed_min > side->speed_max) {
        fprintf(stderr, "%s speed_min must be <= speed_max\n", side_name);
        exit(EXIT_FAILURE);
    }

    if (side->ambulance_percentage < 0 || side->ambulance_percentage > 100) {
        fprintf(stderr, "%s ambulance_percentage must be 0-100\n", side_name);
        exit(EXIT_FAILURE);
    }

    if (side->green_time <= 0) {
        fprintf(stderr, "%s green_time must be > 0\n", side_name);
        exit(EXIT_FAILURE);
    }

    if (side->k_value <= 0) {
        fprintf(stderr, "%s k_value must be > 0\n", side_name);
        exit(EXIT_FAILURE);
    }
}

static void validate_config(Config *config) {

    if (config->bridge_length <= 0) {
        fprintf(stderr, "bridge_length must be > 0\n");
        exit(EXIT_FAILURE);
    }

    if (config->simulation_time <= 0) {
        fprintf(stderr, "simulation_time must be > 0\n");
        exit(EXIT_FAILURE);
    }

    validate_side("East", &config->east);
    validate_side("West", &config->west);
}

/* ========================= */
/* ===== Main Loader ======= */
/* ========================= */

int config_load(const char *filename, Config *config) {

    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening config file");
        return 0;
    }

    char line[MAX_LINE];

    while (fgets(line, sizeof(line), file)) {

        if (is_empty_or_comment(line))
            continue;

        // char *key = strtok(line, "=");
        // char *value = strtok(NULL, "=");
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "\n");

        if (!key || !value)
            continue;

        trim(key);
        trim(value);

        /* -------- General -------- */

        if (strcmp(key, "bridge_length") == 0)
            config->bridge_length = atoi(value);

        else if (strcmp(key, "simulation_time") == 0)
            config->simulation_time = atoi(value);

        else if (strcmp(key, "mode") == 0)
            set_mode(value, config);

        /* -------- EAST -------- */

        else if (strcmp(key, "east_arrival_mean") == 0)
            config->east.arrival_mean = atof(value);

        else if (strcmp(key, "east_speed_min") == 0)
            config->east.speed_min = atoi(value);

        else if (strcmp(key, "east_speed_max") == 0)
            config->east.speed_max = atoi(value);

        else if (strcmp(key, "east_ambulance_percentage") == 0)
            config->east.ambulance_percentage = atoi(value);

        else if (strcmp(key, "east_green_time") == 0)
            config->east.green_time = atoi(value);

        else if (strcmp(key, "east_k_value") == 0)
            config->east.k_value = atoi(value);

        /* -------- WEST -------- */

        else if (strcmp(key, "west_arrival_mean") == 0)
            config->west.arrival_mean = atof(value);

        else if (strcmp(key, "west_speed_min") == 0)
            config->west.speed_min = atoi(value);

        else if (strcmp(key, "west_speed_max") == 0)
            config->west.speed_max = atoi(value);

        else if (strcmp(key, "west_ambulance_percentage") == 0)
            config->west.ambulance_percentage = atoi(value);

        else if (strcmp(key, "west_green_time") == 0)
            config->west.green_time = atoi(value);

        else if (strcmp(key, "west_k_value") == 0)
            config->west.k_value = atoi(value);

        else {
            fprintf(stderr, "Unknown config key: %s\n", key);
            exit(EXIT_FAILURE);
        }
    }

    fclose(file);

    validate_config(config);

    return 1;
}