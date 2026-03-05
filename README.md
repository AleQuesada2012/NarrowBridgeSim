# NarrowBridgeSim

## Project overview
This project is a concurrent programming simulation of the "Narrow Bridge Problem". A city has an obsolete, single-lane bridge connecting the East and West sectors, because of its narrow width, vehicles can only travel in one direction at a time.

The simulation is written entirely in C for Linux environments, utilizing `pthreads` library to manage concurrency, prevent deadlocks, and avoid busy waiting.

##  Features
* **Thread-based Entities:** Every vehicle (car or ambulance) is represented by an independent thread.
* **Strict Ambulance Priority:** Regardless of the active traffic control mode, if an ambulance arrives at the bridge, it is granted passage as soon as the bridge clears.
* **Exponential Traffic Generation:** Vehicle inter-arrival times follow an exponential distribution.
* **Independent Parameters:** The East and West sides operate with completely independent variables for arrival rates, speeds, ambulance probabilities, and traffic control limits.

##  Traffic control modes
The system can be configured to operate under one of three administrative modes:
1. **Carnage Mode:** First-come, first-served. A vehicle can cross if the bridge is empty or if current traffic is flowing in its direction.
2. **Semaphore Mode:** Traffic lights alternate the right of way between the East and West sides based on specific time durations.
3. **Traffic Officer Mode:** A designated number of vehicles are allowed to pass in one direction before yielding to a specific number in the opposite direction.


##  Compilation and execution

**Prerequisites:** A native Linux environment (no virtual machines) with GCC installed.

**Compilation:**
To compile the project, simply run:
```bash
make
```
## Team members
| [@An-Gi](https://github.com/An-Gi) | [@AleQuesada2012](https://github.com/AleQuesada2012) | [@Est3b4nEspSol](https://github.com/Est3b4nEspSol) |
|:---:|:---:|:---:|
| <img src="https://github.com/An-Gi.png?size=100" width="100"> | <img src="https://github.com/AleQuesada2012.png?size=100" width="100"> |<img src="https://github.com/Est3b4nEspSol.png?size=100" width="100"> |
