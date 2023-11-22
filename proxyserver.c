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
#include "proxyserver.h"


/*
 * Constants
 */
#define RESPONSE_BUFSIZE 10000

/*
 * Global configuration variables.
 * Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int num_listener;
int *listener_ports;
int num_workers;
char *fileserver_ipaddr;
int fileserver_port;
int max_queue_size;

void send_error_response(int client_fd, status_code_t err_code, char *err_msg) {
    http_start_response(client_fd, err_code);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    char *buf = malloc(strlen(err_msg) + 2);
    sprintf(buf, "%s\n", err_msg);
    http_send_string(client_fd, buf);
    return;
}


priority_queue *queue;

void serve_request(int client_fd) {

    // create a fileserver socket
    int fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fileserver_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        exit(errno);
    }

    // create the full fileserver address
    struct sockaddr_in fileserver_address;
    fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
    fileserver_address.sin_family = AF_INET;
    fileserver_address.sin_port = htons(fileserver_port);

    // connect to the fileserver
    int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                    sizeof(fileserver_address));
    if (connection_status < 0) {
        // failed to connect to the fileserver
        printf("Failed to connect to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }

    // successfully connected to the file server
    char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));

    // forward the client request to the fileserver
    int bytes = read(client_fd, buffer, RESPONSE_BUFSIZE);
    int ret = http_send_data(fileserver_fd, buffer, bytes);
    if (ret < 0) {
        printf("Failed to send request to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");

    } else {
        // forward the fileserver response to the client
        while (1) {
            int bytes = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
            if (bytes <= 0) // fileserver_fd has been closed, break
                break;
            ret = http_send_data(client_fd, buffer, bytes);
            if (ret < 0) { // write failed, client_fd has been closed
                break;
            }
        }
    }

    // close the connection to the fileserver
    shutdown(fileserver_fd, SHUT_WR);
    close(fileserver_fd);

    // Free resources and exit
    free(buffer);
}

void *listeners(void *arg) {
    int listener = socket(PF_INET, SOCK_STREAM, 0);
    if (listener == -1) {
        perror("Failed to create a new listener socket");
        exit(errno);
    }

    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set listener socket options");
        exit(errno);
    }

    int listener_port = *((int *)arg);

    // create the full address for this listener
    struct sockaddr_in listener_addr;
    memset(&listener_addr, 0, sizeof(listener_addr));
    listener_addr.sin_family = AF_INET;
    listener_addr.sin_addr.s_addr = INADDR_ANY;
    listener_addr.sin_port = htons(listener_port);

    // bind the listener socket to the address and port number specified
    if (bind(listener, (struct sockaddr *)&listener_addr,
             sizeof(listener_addr)) == -1) {
        perror("Failed to bind listener socket");
        exit(errno);
    }

    // starts waiting for the client to request a connection
    if (listen(listener, 1024) == -1) {
        perror("Failed to listen on listener socket");
        exit(errno);
    }

    struct sockaddr_in client_addr;
    size_t client_len = sizeof(client_addr);

    while (1) {
        int client_fd = accept(listener,
                               (struct sockaddr *)&client_addr,
                               (socklen_t *)&client_len);
        if (client_fd < 0) {
            perror("Error accepting socket");
            continue;
        }

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_addr.sin_addr),
               client_addr.sin_port);
       
        char peek[RESPONSE_BUFSIZE];
        int bytes = recv(client_fd, peek, RESPONSE_BUFSIZE - 1, MSG_PEEK);
        peek[bytes] = '\0';

        if (strstr(peek, GETJOBCMD) != NULL) {
            job job;

            if (!get_work_nonblocking(queue, &job)) {
                send_error_response(client_fd, QUEUE_EMPTY, "Queue is empty");
            } else {
                http_start_response(client_fd, OK);
                http_send_header(client_fd, "Content-Type", "text/plain");
                http_end_headers(client_fd);
                http_send_string(client_fd, job.path);
            }
    
            shutdown(client_fd, SHUT_WR);
            close(client_fd);
            continue;
        }

        int priority = 0; 
	    int delay = 0;
        char path[PATH];

        sscanf(peek, "GET %s", path);

        char *prio = strstr(peek, "GET /");
        if (prio != NULL) {
            sscanf(prio, "GET /%d/", &priority);
        }

        char *del = strstr(peek, "Delay: ");
        if (del != NULL) {
            sscanf(del, "Delay: %d", &delay);
        }

        if (add_work(queue, priority, client_fd, delay, path) == -1) {
            send_error_response(client_fd, QUEUE_FULL, "Queue is full\n");
            shutdown(client_fd, SHUT_WR);
            close(client_fd);
        }
    }

    close(listener);
    pthread_exit(NULL);
}

void *workers(void *arg) {
    while (1) {
        job job = get_work(queue);
        int client_fd = job.fd;
        int delay = job.delay;

        if (delay > 0) {
            sleep(delay);
        }

        serve_request(client_fd);

        shutdown(client_fd, SHUT_WR);
        close(client_fd);
    }

    pthread_exit(NULL);
}

int server_fd;
/*
 * opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *server_fd) {
    queue = create_queue(max_queue_size);

    pthread_t listener_threads[num_listener];
    for (int i = 0; i < num_listener; i++) {
        int *listener_port = malloc(sizeof(int));
        *listener_port = listener_ports[i];
        pthread_create(&listener_threads[i], NULL, listeners, (void *)listener_port);
    }

    pthread_t worker_threads[num_workers];
    for (int i = 0; i < num_workers; i++) {
        pthread_create(&worker_threads[i], NULL, workers, NULL);
    }

    for (int i = 0; i < num_listener; i++) {
        pthread_join(listener_threads[i], NULL);
    }

    free(listener_ports);
    free(queue->jobs);
    free(queue); 
    
    for (int i = 0; i < num_workers; i++) {
        pthread_join(worker_threads[i], NULL);
    }

    shutdown(*server_fd, SHUT_RDWR);
    close(*server_fd);
}

/*
 * Default settings for in the global configuration variables
 */
void default_settings() {
    num_listener = 1;
    listener_ports = (int *)malloc(num_listener * sizeof(int));
    listener_ports[0] = 8000;

    num_workers = 1;

    fileserver_ipaddr = "127.0.0.1";
    fileserver_port = 3333;

    max_queue_size = 100;
}

void print_settings() {
    printf("\t---- Setting ----\n");
    printf("\t%d listeners [", num_listener);
    for (int i = 0; i < num_listener; i++)
        printf(" %d", listener_ports[i]);
    printf(" ]\n");
    printf("\t%d workers\n", num_listener);
    printf("\tfileserver ipaddr %s port %d\n", fileserver_ipaddr, fileserver_port);
    printf("\tmax queue size  %d\n", max_queue_size);
    printf("\t  ----\t----\t\n");
}

void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    for (int i = 0; i < num_listener; i++) {
        if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    }
    free(listener_ports);
    exit(0);
}

char *USAGE =
    "Usage: ./proxyserver [-l 1 8000] [-n 1] [-i 127.0.0.1 -p 3333] [-q 100]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_callback_handler);

    /* Default settings */
    default_settings();

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp("-l", argv[i]) == 0) {
            num_listener = atoi(argv[++i]);
            free(listener_ports);
            listener_ports = (int *)malloc(num_listener * sizeof(int));
            for (int j = 0; j < num_listener; j++) {
                listener_ports[j] = atoi(argv[++i]);
            }
        } else if (strcmp("-w", argv[i]) == 0) {
            num_workers = atoi(argv[++i]);
        } else if (strcmp("-q", argv[i]) == 0) {
            max_queue_size = atoi(argv[++i]);
        } else if (strcmp("-i", argv[i]) == 0) {
            fileserver_ipaddr = argv[++i];
        } else if (strcmp("-p", argv[i]) == 0) {
            fileserver_port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }
    print_settings();

    serve_forever(&server_fd);

    return EXIT_SUCCESS;
}
