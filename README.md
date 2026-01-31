# Multithreaded Producer–Consumer (POSIX Threads)

A multithreaded Producer–Consumer application written in C using POSIX threads, mutexes, and semaphores. The program simulates concurrent workloads with multiple producers and consumers sharing a bounded circular buffer, supports clean termination via poison pills, and reports performance metrics (latency + throughput). 

## Features

* Multiple producer and consumer threads (configurable at runtime)
* Bounded circular buffer (efficient constant-time insert/remove)
* Synchronization using:

  * Mutex (`pthread_mutex_t`) for critical sections
  * Semaphores (`empty` and `full`) to block when the buffer is full/empty
* Poison-pill termination for graceful consumer shutdown
* Thread-safe random generation (`rand_r`)
* Performance statistics: average latency and throughput 

## Repository Structure

* src/MP.c
* report/CMP310-Group-Project-Report.pdf
* README.md
* .gitignore

## Requirements

* GCC (or a C compiler with pthread support)
* POSIX threads & semaphores
  Recommended: Linux environment (Ubuntu / WSL / VM). On Windows, use WSL (Ubuntu) to compile/run easily.

## Build (Compile)

If your code is inside src/:

* gcc -o MP src/MP.c -pthread

If your code is in the repository root:

* gcc -o MP MP.c -pthread 

## Run

* ./MP <producers> <consumers> <buffer_size> 

All parameters must be positive integers. Invalid input terminates the program. 

## Example Runs

Recommended example:

* ./MP 3 2 10
  Each producer generates 20 items (total 60). 

Stress test:

* ./MP 5 3 20 

Small buffer test (forces blocking):

* ./MP 4 4 2 

Large-scale test:

* ./MP 20 20 50 

Minimum valid parameters:

* ./MP 1 1 2 

## Output and Statistics

During execution, producers print produced items (including priority), and consumers print consumed items along with measured latency. At the end, the program prints final statistics including:

* Total items produced
* Total items consumed
* Total execution time
* Average latency
* Throughput (items/second)
* Buffer size and items per producer 

## Implementation Notes

* Circular buffer indices use modular arithmetic for efficient wraparound.
* Producers wait on `empty`, lock the mutex, insert, then post `full`.
* Consumers wait on `full`, lock the mutex, remove, then post `empty`.
* After all producers finish, the main thread inserts one poison pill per consumer so each consumer terminates cleanly. 

## Authors

CMP310 (Operating Systems) Group Project — Fall 2025
American University of Sharjah 
Farah Tawalbeh
Yafa Asia
Shaher Ghrewati
Ahmad Al-Nakaleh
