#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>               //Necessary to find out the throughput
#include <errno.h>              //Prints errors clearly 

#define POISON_PILL -1
#define ITEMS_PER_PRODUCER 20
#define MAX_URGENT_RATIO 4  // 25% urgent items, implemented as a modulus to a random number generated

typedef struct {
    int data;
    int priority;  // 0 = normal, 1 = urgent
    struct timespec enqueue_time;       //This will store the time when the buffer item was created
} buffer_item;

typedef struct {
    buffer_item *buffer;
    int size;
    int in, out;
    pthread_mutex_t mutex;
    sem_t empty, full;
    
    // Statistics parameters for printing at the end
    int items_produced;
    int items_consumed;
    double total_latency;
    struct timespec start_time, end_time;
} shared_state_t;

static shared_state_t shared;   //now ALL the threads have shared mutex, empty semaphore, full semaphore,...  and are accessed through &shared

// Functions declarations
void* producer(void* arg);
void* consumer(void* arg);
int insert_item(buffer_item item);
int remove_item(buffer_item *item);
double timespec_diff(struct timespec *start, struct timespec *end);       //Function will return the difference   
void print_statistics(void);

void* producer(void* arg) {

    //We did double casting because threads excpect long or void*
    int id = (int)(long)arg;        

    //very cool way of creating a seed using the thread's own ID such that the value is unique
    unsigned int seed = time(NULL) + id;
    printf("[Producer-%d] Started\n", id);
    
    //loops 20 times for each item 
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) { 

        // Create item with random data and priority
        buffer_item item;

	//rand_r generates thread safe random numbers that avoid race conditions
        item.data = rand_r(&seed) % 1000;

        //This is just a fancy way we used to figure out priority by just doing mod 4 (= MAX_URGENT_RATIO) and making it urgent if it is = to 0          
        item.priority = (rand_r(&seed) % MAX_URGENT_RATIO == 0) ? 1 : 0;

        //used to do a timestamp. will use this value later for the latency calculation
        clock_gettime(CLOCK_MONOTONIC, &item.enqueue_time); 

        //Very important because we dont want all the producers to immediately produce and fill out the buffer. having them produce at semi-random times is more realistic 
        usleep(50000 + (rand_r(&seed) % 100000));

        // Critical section: Start of mutex part
        sem_wait(&shared.empty);
        pthread_mutex_lock(&shared.mutex);

        if (insert_item(item) == 0) {
            shared.items_produced++;

	    //some syntax just to say 1: URGENT, 0: normal. %d is the ID of the producer
            printf("[Producer-%d] Produced item: %d (%s)\n", id, item.data, item.priority ? "URGENT" : "normal");
        }

        pthread_mutex_unlock(&shared.mutex);
        sem_post(&shared.full);
        // End of mutex part. note that wait and "post" are outside the mutex section to avoid a producer block
    }
    
    printf("[Producer-%d] Finished producing %d items\n", id, ITEMS_PER_PRODUCER);
    return NULL;
}

void* consumer(void* arg) {
    // Convert thread argument to integer ID
    int id = (int)(long)arg;

    // Each thread gets its own random seed to avoid race conditions with rand()
    unsigned int seed = time(NULL) + id;
    
    printf("[Consumer-%d] Started\n", id);
    
    while (1) {
        // Wait until there is at least 1 filled slot in the buffer
        sem_wait(&shared.full);

        // Lock buffer for safe removal
        pthread_mutex_lock(&shared.mutex);

        buffer_item item;

        // Attempt to remove an item from the buffer
        // If failed (should not happen), release locks and retry. This breaks the deadlock condition. 
        if (remove_item(&item) != 0) {
            pthread_mutex_unlock(&shared.mutex);
            sem_post(&shared.full);
            continue;
        }

        // Release the mutex after safely removing the item
        pthread_mutex_unlock(&shared.mutex);

        // Signal that one more empty slot is available
        sem_post(&shared.empty);
        
        // Check if the removed item is a poison pill
        // If yes: exit the loop and terminate this consumer thread
        if (item.data == POISON_PILL) {
            printf("[Consumer-%d] Received poison pill. Terminating.\n", id);
            break;
        }
        
        // Random sleep to simulate processing time (75–225 ms)
        usleep(75000 + (rand_r(&seed) % 150000));
        
        // Record the time at which the item was consumed (for latency)
        struct timespec dequeue_time;
        clock_gettime(CLOCK_MONOTONIC, &dequeue_time);

        // Calculate latency = dequeue_time - enqueue_time
        double latency = timespec_diff(&item.enqueue_time, &dequeue_time);

        // Fixed race conditions : protect stats updates
        // This prevents race conditions when multiple consumers update global stats
        pthread_mutex_lock(&shared.mutex);
        shared.total_latency += latency;	//accumulate latency
        shared.items_consumed++;		//increase consumed items
        pthread_mutex_unlock(&shared.mutex);
        
        // Print consumer action + latency + priority info
        printf("[Consumer-%d] Consumed item: %d (%s, Latency: %.3fs)\n", 
               id, item.data, item.priority ? "URGENT" : "normal", latency);
    }

    // Reached only after receiving a poison pill
    printf("[Consumer-%d] Finished consuming\n", id);
    return NULL;
}
// Insert an item into the circular buffer.
// Returns 0 on success, -1 if the buffer is full.
int insert_item(buffer_item item) {

    // (next_position == out) means no space left
    if ((shared.in + 1) % shared.size == shared.out) {
        return -1; 
    }

    // Place item into current index
    shared.buffer[shared.in] = item;

    // Move write index forward in a circular manner (increment shared.in)
    shared.in = (shared.in + 1) % shared.size;
    return 0;
}

// Returns 0 on success, -1 if the buffer is empty.
int remove_item(buffer_item *item) {

    // Empty condition: in == out
    if (shared.in == shared.out) {
        return -1; 
    }
    

    // Copy out the item
    *item = shared.buffer[shared.out];

    // Move read index forward (increment shared.out)
    shared.out = (shared.out + 1) % shared.size;
    return 0;
}

// Compute the difference between two timestamps in seconds
double timespec_diff(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) +
           (end->tv_nsec - start->tv_nsec) / 1e9;		//the time returned with this function is in nanoseconds. we must divide by 10^9 to return to seconds
}

// Print final program statistics: total items, timing, latency, throughput. Called at the end
void print_statistics(void) {

    // Capture end time of program
    clock_gettime(CLOCK_MONOTONIC, &shared.end_time);

    // Total execution time (start → end)
    double total_time = timespec_diff(&shared.start_time, &shared.end_time);
    
    printf("\n=== FINAL STATISTICS ===\n");
    printf("Total items produced: %d\n", shared.items_produced);
    printf("Total items consumed: %d\n", shared.items_consumed);
    printf("Total execution time: %.3f seconds\n", total_time);
    
    // Only print latency and throughput if something was consumed
    if (shared.items_consumed > 0) {
        printf("Average latency: %.3f seconds\n",
               shared.total_latency / shared.items_consumed);
        printf("Throughput: %.2f items/second\n",
               shared.items_consumed / total_time);
    }
    
    printf("Buffer size: %d\n", shared.size);
    printf("Items per producer: %d\n", ITEMS_PER_PRODUCER);
}

int main(int argc, char *argv[]) {

    // Must supply 3 arguments: producers, consumers, buffer_size
    if (argc != 4) {
        printf("Usage: %s <producers> <consumers> <buffer_size>\n", argv[0]);
        return 1;
    }
    
    //Capturing the values from the argument vector
    int producers = atoi(argv[1]);      
    int consumers = atoi(argv[2]);
    int buffer_size = atoi(argv[3]);
    
    // The following IF condtions are to Validate command-line input (checking that input makes sense after capturing)
    if (producers <= 0 || consumers <= 0 || buffer_size <= 0) {
        printf("Error: All parameters must be positive integers\n");
        return 1;
    }
    
    if (buffer_size < 2) {
        printf("Error: Buffer size must be at least 2\n");
        return 1;
    }
    
    printf("=== Producer-Consumer Application ===\n");

    // Allocate memory for the buffer dynamically
    shared.buffer = malloc(buffer_size * sizeof(buffer_item));
    if (!shared.buffer) {
        perror("Failed to allocate buffer");
        return 1;
    }

    //Start of initialization
    // Initialize shared structure fields
    shared.size = buffer_size;
    shared.in = shared.out = 0;
    shared.items_produced = shared.items_consumed = 0;
    shared.total_latency = 0.0;         

    //The following IF conditions are here to ensure that errors do not happen during initialization (Deliverable requirement)
    // Initialize mutex
    if (pthread_mutex_init(&shared.mutex, NULL) != 0) {
        perror("Mutex initialization failed");
        free(shared.buffer);
        return 1;
    }

    // empty semaphore starts at buffer_size (all slots empty)
    if (sem_init(&shared.empty, 0, buffer_size) != 0) {
        perror("Empty semaphore initialization failed");
        pthread_mutex_destroy(&shared.mutex);
        free(shared.buffer);
        return 1;
    }

    // full semaphore starts at 0 (nothing produced yet)
    if (sem_init(&shared.full, 0, 0) != 0) {
        perror("Full semaphore initialization failed");
        sem_destroy(&shared.empty);
        pthread_mutex_destroy(&shared.mutex);
        free(shared.buffer);
        return 1;
    }
    // End of initialization

    // Record start time for statistics. (start the actual program after we checked that all semaphores, mutex are initialized correctly)
    clock_gettime(CLOCK_MONOTONIC, &shared.start_time);        
    
    pthread_t prod_threads[producers], cons_threads[consumers];
    int i;

    // Create producer threads (& simultaneously check if it was created correctly)
    for (i = 0; i < producers; i++) {
        if (pthread_create(&prod_threads[i], NULL, producer, (void*)(long)(i+1)) != 0) {
            perror("Failed to create producer thread");
            goto cleanup;
        }
    }

    // Create consumer threads
    for (i = 0; i < consumers; i++) {
        if (pthread_create(&cons_threads[i], NULL, consumer, (void*)(long)(i+1)) != 0) {
            perror("Failed to create consumer thread");
            goto cleanup;
        }
    }


    // Wait for all producers to finish. after they finish we join them.    
    for (i = 0; i < producers; i++) {
        pthread_join(prod_threads[i], NULL);
    }
    
    printf("\nAll producers finished. Inserting poison pills...\n");
    

    // Construct poison pill item
    buffer_item poison_item = {POISON_PILL, 0, {0, 0}};

    // One poison pill per consumer
    for (i = 0; i < consumers; i++) {
        sem_wait(&shared.empty);
        pthread_mutex_lock(&shared.mutex);

        insert_item(poison_item);
        printf("Inserted poison pill %d/%d\n", i+1, consumers);

        pthread_mutex_unlock(&shared.mutex);
        sem_post(&shared.full);
    }
    
    // Wait for all consumers to terminate
    for (i = 0; i < consumers; i++) {
        pthread_join(cons_threads[i], NULL);
    }
    
    // Call the function to print all statistics at the end
    print_statistics();
    
//This here is where the program goes to if ANY thread creation was done wrong (producer or consumer threads) by releasing all resources. Will also be called naturally at the end of main
cleanup:

    // Free all system resources
    free(shared.buffer);
    pthread_mutex_destroy(&shared.mutex);
    sem_destroy(&shared.empty);
    sem_destroy(&shared.full);
    
    printf("\nProgram completed successfully!\n");
    return 0;
}
//To run the program, you must compile with the real-time library -pthread since it is not a standard C library
//osc@ubuntu:~$ gcc Project-commented.c -o Project-commented -pthread
//osc@ubuntu:~$ ./Project-commented 3 2 10	(example: producers = 3, consumers = 2, buffer size = 10)
