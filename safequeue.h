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

typedef struct {
    char *path;
    int priority;  
    int delay;  
} Job;

typedef struct {
    Job *jobs;  
    int capacity;  
    int size;  
    pthread_mutex_t lock;  
    pthread_cond_t cond;  
} PriorityQueue;

extern PriorityQueue* create_queue();

extern void add_work(PriorityQueue *queue, Job job);

extern Job get_work(PriorityQueue *queue);

extern Job get_work_nonblocking(PriorityQueue *queue);