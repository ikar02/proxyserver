#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H
#define PATH 256

typedef struct {
    int fd;
    int priority;
    int delay;
    char path[PATH];
} job;


typedef struct {
    job *jobs;
    int capacity;
    int size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} priority_queue;

priority_queue *create_queue(int capacity);

int add_work(priority_queue *queue, int priority, int client_fd, int delay, const char *path);

job get_work(priority_queue *queue);

int get_work_nonblocking(priority_queue *queue, job *item);

#endif 
