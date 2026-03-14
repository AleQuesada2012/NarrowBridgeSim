#ifndef MODES_H
#define MODES_H

#include <pthread.h>
#include "config.h"
#include "bridge.h"

pthread_t start_mode_controller(const Config *config, Bridge *bridge);

#endif