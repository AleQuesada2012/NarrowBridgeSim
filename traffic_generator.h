#ifndef TRAFFIC_GENERATOR_H
#define TRAFFIC_GENERATOR_H

#include "config.h"
#include "bridge.h"

/* ============================= */
/* ===== Traffic Generator ===== */
/* ============================= */

/*
 * Starts the traffic simulation.
 * Blocks until simulation ends.
 */
void traffic_generator_start(const Config *config, Bridge *bridge);

#endif