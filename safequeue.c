#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include "safequeue.h"

priority_queue *create_queue(int capacity) {
    priority_queue *queue = (priority_queue *)malloc(sizeof(priority_queue));

    if (!queue) {
        perror("Failed to create queue");
        exit(EXIT_FAILURE);
    }

    queue->jobs = (job *)malloc(capacity * sizeof(job));
    if (!queue->jobs) {
        perror("Failed to allocate memory for job array");
        exit(EXIT_FAILURE);
    }

    queue->capacity = capacity;
    queue->size = 0;

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);

    return queue;
}

int add_work(priority_queue *queue, int priority, int client_fd, int delay, const char *path) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->size == queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    int idx = 0;
    while (idx < queue->size && priority <= queue->jobs[idx].priority) {
        idx++;
    }

    for (int i = queue->size; i > idx; i--) {
        queue->jobs[i] = queue->jobs[i - 1];
    }

    queue->size++;
    strncpy(queue->jobs[idx].path, path, PATH);
    queue->jobs[idx].fd = client_fd;
    queue->jobs[idx].priority = priority;
    queue->jobs[idx].delay = delay;
 
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

    return 0; 
}

job get_work(priority_queue *queue) {
    pthread_mutex_lock(&queue->mutex);

    while (queue->size == 0) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    job work = queue->jobs[0];
    for (int i = 0; i < queue->size - 1; i++) {
        queue->jobs[i] = queue->jobs[i + 1];
    }
    queue->size--;

    pthread_mutex_unlock(&queue->mutex);

    return work;
}

int get_work_nonblocking(priority_queue *queue, job *work) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    *work = queue->jobs[0];
    for (int i = 0; i < queue->size - 1; i++) {
        queue->jobs[i] = queue->jobs[i + 1];
    }
    queue->size--;

    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

