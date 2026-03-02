#ifndef CONFIG_H
#define CONFIG_H

typedef enum {
    MODE_CARNAGE,
    MODE_SEMAPHORE,
    MODE_OFFICER
} Mode;

typedef struct {
    double arrival_mean;
    int speed_min;
    int speed_max;
    int ambulance_percentage;
    int green_time;
    int k_value;
} SideConfig;

typedef struct {
    int bridge_length;
    int simulation_time;   // opcional pero recomendable
    SideConfig east;
    SideConfig west;
    Mode mode;
} Config;

int config_load(const char *filename, Config *config);

#endif