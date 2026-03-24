# NarrowBridgeSim

## Overview

A concurrent simulation of the **Narrow Bridge Problem** written entirely in C. A city has a single-lane bridge connecting its East and West sectors. Because of its width, vehicles can only travel in one direction at a time. The simulation models vehicle traffic, synchronization, and three distinct traffic-control strategies using POSIX threads.

---

## Project structure

```
NarrowBridgeSim/
├── src/
│   ├── main.c                # Entry point; loads config, starts controllers
│   ├── bridge.c              # Bridge state, FIFO queues, synchronization logic
│   ├── vehicle.c             # Vehicle thread routine and per-meter slot advancement
│   ├── traffic_generator.c   # Exponential inter-arrival generator (one thread per side)
│   ├── config.c              # .config file parser and validator
│   ├── semaphore_ctrl.c      # Two independent traffic-light threads (SEMAPHORE mode)
│   └── officer_ctrl.c        # Two independent officer threads (OFFICER mode)
├── include/
│   ├── bridge.h
│   ├── vehicle.h
│   ├── traffic_generator.h
│   ├── config.h
│   ├── semaphore_ctrl.h
│   └── officer_ctrl.h
├── gui.c                     # Standalone SDL2 visualizer (reads simulation stdout via pipe)
├── Makefile
└── bridge.config             # Default configuration file
```

---

## How it works

### Threads

Every entity in the simulation is an independent thread:

| Entity | Thread(s) |
|---|---|
| Each vehicle | One thread per vehicle, spawned by the generator |
| Traffic generator | One thread per side (EAST, WEST) |
| Semaphore controller | One thread per traffic light (SEMAPHORE mode only) |
| Officer controller | One thread per officer (OFFICER mode only) |

### Bridge synchronization

Each vehicle thread calls `bridge_enter`, where it is placed into a **per-side FIFO queue** (a singly-linked list, stack-allocated per thread). Vehicles wait on individual `pthread_cond_t` condition variables and are woken only when they reach the head of the queue and the bridge conditions permit entry. This design prevents spurious wake-ups, avoids busy-waiting, and enforces strict arrival-order fairness.

The bridge is modelled as an array of `length` mutexes — one per meter. A vehicle holds `slots[i]` and acquires `slots[i+1]` before releasing it, producing sequential, collision-free movement.

### Ambulance priority

In all three modes, an ambulance waiting at the head of its queue causes normal cars on the opposite side to yield. Ambulances can also cross against their traffic control signal (red light, blocked officer direction) as long as the bridge is completely clear of oncoming traffic. They enter without consuming a K-slot when the officer's quota for their side is already exhausted.

---

## Traffic control modes

### 1. Carnage
No controller threads. Vehicles enter the bridge freely as long as it is empty or already flowing in their direction. Direction alternates automatically when the bridge clears.

### 2. Semaphore
Two independent threads — one per traffic light — coordinate using `pthread_cond_signal`. Each thread holds its green phase for a configured duration, then signals the other thread to take over. The first side to go green is chosen by a random coin toss at startup. Timers are wall-clock based and are not paused by bridge activity or ambulance crossings.

### 3. Officer
Two independent threads — one per side — share the bridge through turn-passing via `pthread_cond_broadcast`. Each officer allows up to **K** vehicles from its side to enter the bridge before yielding. Ambulances count against K while the quota is live; once K reaches zero an ambulance at the head receives a free privilege pass. An officer yields its turn early if its side has no vehicles waiting and the opposite side does.

---

## Configuration

The simulation is driven by a plain-text `.config` file. The default file is `bridge.config`.

```ini
# ── Global ──────────────────────────────────────────────────────
bridge_length       = 100     # meters (also the maximum simultaneous vehicles)
simulation_time     = 60      # seconds
mode                = CARNAGE # CARNAGE | SEMAPHORE | OFFICER

# ── Per-side parameters (prefix east_ or west_) ─────────────────
east_arrival_mean         = 5     # mean inter-arrival time (seconds, exponential)
east_speed_min            = 40    # km/h
east_speed_max            = 60    # km/h
east_ambulance_percentage = 0.25  # fraction of vehicles that are ambulances (0.0–1.0)
east_green_time           = 5     # green phase duration in seconds (SEMAPHORE mode)
east_k_value              = 3     # vehicles per turn (OFFICER mode)

west_arrival_mean         = 4
west_speed_min            = 35
west_speed_max            = 55
west_ambulance_percentage = 0.15
west_green_time           = 4
west_k_value              = 2
```

Lines beginning with `#` are treated as comments. Key-value pairs are separated by `=`. Unknown keys cause the program to exit with an error.

---

## Graphical interface

A separate `bridge_gui` process reads the simulation's standard output through a Unix pipe and renders a live SDL2 window at ~15 fps. No X11 or XQuartz is required.

```
./bridge_sim bridge.config | ./bridge_gui
```

The window shows:

- **Bridge lane** with meter tick marks and vehicles advancing in real time, labelled by ID
- **Waiting queues** on each side in FIFO arrival order (head nearest the bridge)
- **Mode indicator** — flow arrows (Carnage), traffic lights with green/red state (Semaphore), or officer panels with K quota and progress bar (Officer)
- **Stats panel** — vehicles passed per side, vehicles currently on bridge
- **Console log panel** — the last 13 human-readable log lines, colour-coded by event type

The GUI communicates with the simulation exclusively through structured log lines embedded in stdout (prefixed `[SLOT]`, `[QUEUE]`, `[DIRECTION]`, `[LIGHT]`, `[OFFICER]`, `[MODE]`). The simulation itself requires no modification to support the GUI and produces complete console output independently.

---

## Dependencies and compilation

### Ubuntu (x86-64)

```bash
sudo apt install libsdl2-dev
make          # builds bridge_sim and bridge_gui
```

### macOS (Apple Silicon or Intel)

```bash
brew install sdl2
make          # sdl2-config is located automatically
```

### Make targets

| Target | Description |
|---|---|
| `make` | Build both `bridge_sim` and `bridge_gui` |
| `make run` | Build and run the simulation in the terminal |
| `make run-gui` | Build and launch simulation piped into the GUI |
| `make clean` | Remove build artefacts and binaries |
| `make re` | Clean then rebuild everything |

A custom config file can be passed via the `CONFIG` variable:

```bash
make run CONFIG=my_scenario.config
make run-gui CONFIG=my_scenario.config
```

---

## Team members

| [@An-Gi](https://github.com/An-Gi) | [@AleQuesada2012](https://github.com/AleQuesada2012) | [@Est3b4nEspSol](https://github.com/Est3b4nEspSol) |
|:---:|:---:|:---:|
| <img src="https://github.com/An-Gi.png?size=100" width="100"> | <img src="https://github.com/AleQuesada2012.png?size=100" width="100"> | <img src="https://github.com/Est3b4nEspSol.png?size=100" width="100"> |