#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "safequeue.h"


// C code to implement Priority Queue 
// using Linked List 
#include <stdio.h> 
#include <stdlib.h> 

PriorityQueue* create_queue(int capacity) {
    PriorityQueue *queue = malloc(sizeof(PriorityQueue));
    queue->jobs = malloc(sizeof(Job) * capacity);
    queue->capacity = capacity;
    queue->size = 0;
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->cond, NULL);
    return queue;
}

void add_work(PriorityQueue *queue, Job job) {
    pthread_mutex_lock(&queue->lock);

    if (queue->size == queue->capacity) {
        // Queue is full
        pthread_mutex_unlock(&queue->lock);
        return;
    }

    // Add job to the queue and heapify
    queue->jobs[queue->size] = job;
    int i = queue->size;
    queue->size++;

    // Heapify up
    while (i != 0 && queue->jobs[(i - 1) / 2].priority < queue->jobs[i].priority) {
        // Swap
        Job temp = queue->jobs[(i - 1) / 2];
        queue->jobs[(i - 1) / 2] = queue->jobs[i];
        queue->jobs[i] = temp;

        i = (i - 1) / 2;
    }

    pthread_cond_signal(&queue->cond);  // Signal any waiting threads
    pthread_mutex_unlock(&queue->lock);
}

Job get_work(PriorityQueue *queue) {
    pthread_mutex_lock(&queue->lock);

    while (queue->size == 0) {
        pthread_cond_wait(&queue->cond, &queue->lock);  // Wait for a job to be added
    }

    // Get the highest priority job
    Job job = queue->jobs[0];
    queue->size--;

    // Move the last job to the root and heapify down
    queue->jobs[0] = queue->jobs[queue->size];

    int i = 0;
    while ((2 * i + 1) < queue->size) {
        int max = 2 * i + 1;
        if ((2 * i + 2) < queue->size && queue->jobs[2 * i + 2].priority > queue->jobs[max].priority) {
            max = 2 * i + 2;
        }

        if (queue->jobs[i].priority >= queue->jobs[max].priority) {
            break;
        }

        // Swap
        Job temp = queue->jobs[i];
        queue->jobs[i] = queue->jobs[max];
        queue->jobs[max] = temp;

        i = max;
    }

    pthread_mutex_unlock(&queue->lock);
    return job;
}

Job get_work_nonblocking(PriorityQueue *queue) {
    pthread_mutex_lock(&queue->lock);

    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        return (Job){ .path = NULL };  // Return empty job to indicate queue is empty
    }

    // Similar logic as get_work, but non-blocking
    Job job = queue->jobs[0];
    queue->size--;
    queue->jobs[0] = queue->jobs[queue->size];

    int i = 0;
    while ((2 * i + 1) < queue->size) {
        // Same heapify down logic as get_work
    }

    pthread_mutex_unlock(&queue->lock);
    return job;
}
