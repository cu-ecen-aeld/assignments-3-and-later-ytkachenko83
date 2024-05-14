#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <time.h>
#include "aesdsocket.h"
#include "datafile.h"

#define SERVER_PORT "9000"
#define LISTEN_BACKLOG 10
#define NEWLINE '\n'
#define ISO_2822_TIME_FMT "%a, %d %b %Y %T %z"

int server_fd;
timer_t timer_id;
pthread_mutex_t mutex;
volatile sig_atomic_t stopApp;

void cleanup() {
    if (server_fd > 0) {
        close(server_fd);
    }

    destroy_datafile();
    closelog();
}

static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        stopApp = 1;
        syslog(LOG_DEBUG, "%s", "Caught signal, exiting");

        if (server_fd > 0) {
            while (shutdown(server_fd, SHUT_RDWR) == -1) {
                syslog(LOG_ERR, "Server socket shutdown error: %s", strerror(errno));
            }
            close(server_fd);
        }
        // delete timer
        if (timer_id && timer_delete(timer_id) != 0) {
            syslog(LOG_ERR, "Failure to delete timer: %s", strerror(errno));
        }

        // looping thru connections and terminate threads
        clientconn_info *conn;
        SLIST_FOREACH(conn, &connections, next) {
            pthread_cancel(conn->thread_id);
        }
        cleanup_term_conn(1);
        pthread_mutex_destroy(&mutex);

        destroy_datafile();
        closelog();
    }
}

void make_daemon() {
    // we can use daemon syscall or use two-fork approach
    int pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        exit(EXIT_FAILURE);
    }

    if ((pid = fork()) < 0) {
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // file perm
    umask(0);
    // change dir /
    chdir("/");
    // close all open file descriptors
    // for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
    //     close(x);
    // }
}

int send_response(int data_fd, int client_fd) {
    char readbuf[1024*100];
    int m;
    // position to the beginning
    adjust_datafile_pos(data_fd, 0, SEEK_SET);

    while((m = read(data_fd, readbuf, sizeof(readbuf))) > 0) {
        int rc = send(client_fd, readbuf, m, 0);
        if (rc < 0) {
            return rc;
        }
    }

    return 0;
}

void* connnection_handler(void* param) {
    struct aesdsocketclientconn* args = (struct aesdsocketclientconn *) param;

    pthread_cleanup_push(connection_cleanup, args);

    syslog(LOG_DEBUG, "Accepted connection from %s", args->client_ip_addr);

    size_t maxbuflen = args->init_buffer_size;
    char recv_buf[BUFFER_SIZE];
    int n;
    size_t buflen = 0;
    *args->buffer[0] = '\0';

    while((n = recv(args->client_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {
        int data_fd = open_datafile();
        args->data_fd = data_fd;
        if (adjust_datafile_pos(data_fd, 0, SEEK_END) > -1) {
            // locate position of newline
            int k = 0;
            int has_nl = 0;
            for (; k < n; k++) if (recv_buf[k] == NEWLINE) {
                has_nl = 1;
                break;
            }
            int currlen = n;
            if (k+1 < n) {
                currlen = k+1;
            }

            append_string(args->buffer, &maxbuflen, buflen, recv_buf, currlen);
            buflen += currlen;

            if (has_nl) {
                int rc = pthread_mutex_lock(args->mutex);
                if (rc != 0) {
                    syslog(LOG_ERR, "Failure to lock mutex with error code: %d", rc);
                    break;
                }
                // append to the data file
                append_datafile(data_fd, *args->buffer, strlen(*args->buffer));
                *args->buffer[0] = '\0'; // make string empty!
                buflen = 0;

                rc = send_response(data_fd, args->client_fd);
                if (rc < 0) {
                    syslog(LOG_ERR, "Failure to send response to the client - %s", args->client_ip_addr);
                    break;
                }
                if ((rc = pthread_mutex_unlock(args->mutex)) != 0) {
                    syslog(LOG_ERR, "Failure to unlock mutex. Error code: %d", rc);
                    break;
                }
                // data file position will be EOF and we can copy any bytes after NEWLINE
                if (k < n) {
                    buflen = n-k-1;
                    append_string(args->buffer, &maxbuflen, buflen, &recv_buf[k+1], buflen);
                }
            }
        } else {
            syslog(LOG_ERR, "Failure to reposition cursor in the data file %d to the EOF", data_fd);
            break;
        }
    }

    pthread_cleanup_pop(1);

    return args;
}

void connection_cleanup(void* param) {
    struct aesdsocketclientconn* args = (struct aesdsocketclientconn *)param;
    
    close(args->client_fd);
    syslog(LOG_DEBUG, "Closed connection from %s", args->client_ip_addr);
    if (args->data_fd) {
        close_datafile(args->data_fd);
    }
    free(*args->buffer);
    free(args->buffer);
    free(args->client_ip_addr);
    pthread_mutex_unlock(args->mutex); // don't need to handle failures since mutex is PTHREAD_MUTEX_ERRORCHECK
}

void accept_conn() {
    struct sockaddr client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int client_fd = accept(server_fd, &client_addr, &client_addr_len);
    if (client_fd == -1) {
        syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
        return;
    }
    // get client ip
    struct sockaddr_in *client_inaddr = (struct sockaddr_in *) &client_addr;
    char *client_ip_addr = inet_ntoa(client_inaddr->sin_addr);

    pthread_t thread_id;
    struct aesdsocketclientconn* conn_data = malloc(sizeof *conn_data);
    conn_data->mutex = &mutex;
    conn_data->client_fd = client_fd;
    conn_data->client_ip_addr = malloc(strlen(client_ip_addr)+1);
    strcpy(conn_data->client_ip_addr, client_ip_addr);
    conn_data->data_fd = 0;
    conn_data->init_buffer_size = BUFFER_SIZE;
    conn_data->buffer = malloc(sizeof(char *));
    *conn_data->buffer = malloc(BUFFER_SIZE);

    int rc = pthread_create(&thread_id, NULL, connnection_handler, conn_data);
    if (rc != 0) {
        syslog(LOG_ERR, "Failure to create thread. Error code: %d", rc);        
        free(conn_data);
    } else {
        // track thread for later monitoring
        clientconn_info* conn = malloc(sizeof(*conn));
        conn->thread_id = thread_id;
        conn->ref = conn_data;
        // conn->conn_status = conn_data->status;
        SLIST_INSERT_HEAD(&connections, conn, next);
        // look thru LL and determine if any connections has ended
        cleanup_term_conn(0);
    }
}

void cleanup_term_conn(int await_termination) {
    clientconn_info *item, *tmp_item;

    SLIST_FOREACH_SAFE(item, &connections, next, tmp_item) {
        // try join
        int rc;
        if (await_termination != 0) {
            rc = pthread_join(item->thread_id, NULL);
        } else {
            rc = pthread_tryjoin_np(item->thread_id, NULL);
        }
        if (rc == 0) {
            SLIST_REMOVE(&connections, item, _clientconn_info, next);
            free(item->ref);
            free(item);
        }
    }
}

int register_sighandlers() {
    struct sigaction app_action;
    memset(&app_action, 0, sizeof(sigaction));
    app_action.sa_handler = signal_handler;
    sigemptyset (&app_action.sa_mask);
    app_action.sa_flags = 0;

    if (sigaction(SIGTERM, &app_action, NULL) != 0) {
        perror("Failure to register SIGTERM handler");
        return -1;
    }
    if (sigaction(SIGINT, &app_action, NULL) != 0) {
        perror("Failure to register SIGINT handler");
        return -1;
    }

    return 0;
}

int start_server(const char *port, int as_daemon) {

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "%s", "Failure to open socket");
        cleanup();
        exit(EXIT_FAILURE);
    }
    // set SO_REUSEADDR
    const int enable = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        syslog(LOG_ERR, "Failed to set socket options - %s", "SO_REUSEADDR");
        cleanup();
        exit(EXIT_FAILURE);
    }

    // get addr
    struct addrinfo *addr;
    struct addrinfo hints;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(NULL, port, &hints, &addr);
    if (rc != 0) {
        syslog(LOG_ERR, "Failure to obtain address: return code = %d", rc);
        cleanup();
        exit(EXIT_FAILURE);
    }

    rc = bind(server_fd, addr->ai_addr, sizeof(struct sockaddr));
    freeaddrinfo(addr);
    if (rc != 0) {
        syslog(LOG_ERR, "Unable to bind server on port %s: %s", SERVER_PORT, strerror(errno));
        cleanup();
        exit(EXIT_FAILURE);
    }
    
    if (as_daemon) make_daemon();

    rc = listen(server_fd, LISTEN_BACKLOG);
    if (rc != 0) {            
        syslog(LOG_ERR, "Failure to listen socket: %s", strerror(errno));
        cleanup();
        exit(EXIT_FAILURE);
    }
    syslog(LOG_DEBUG, "Listening for connections on port %s", SERVER_PORT);

    return server_fd;
}

static void timer_action(union sigval arg) {
    int rc = pthread_mutex_lock(&mutex);
    if (rc != 0) {
        syslog(LOG_ERR, "Error locking thread data: %s", strerror(errno));
    } else {
        struct timespec ts;
        rc = clock_gettime(CLOCK_REALTIME, &ts);
        if (rc != 0) {
            syslog(LOG_ERR, "Failure to get system wall clock time: %s", strerror(errno));
        } else {
            char dt[100], ts_row[120];
            struct tm tm;
            tzset();
            localtime_r(&ts.tv_sec, &tm);
            size_t len = strftime(dt, 100, ISO_2822_TIME_FMT, &tm);
            strcpy(ts_row, "timestamp:");
            strncat(ts_row, dt, len);
            len = strlen(ts_row);
            ts_row[len] = NEWLINE;
            ts_row[len+1] = '\0';

            int fd = open_datafile();
            if (adjust_datafile_pos(fd, 0, SEEK_END) > -1) 
                append_datafile(fd, ts_row, strlen(ts_row));
            else
                syslog(LOG_ERR, "Failure to adjust data file to position to the end!");
            close_datafile(fd);
        }
        if (pthread_mutex_unlock(&mutex) != 0) {
            syslog(LOG_ERR, "Failed to unlock thread data: %s", strerror(errno));
        }
    }
}

void create_timer() {
    struct sigevent sev;
    memset(&sev, 0, sizeof(struct sigevent));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = timer_action;

    if (timer_create(CLOCK_MONOTONIC, &sev, &timer_id) != 0) {
        syslog(LOG_ERR, "Failure to create timer: %s", strerror(errno));
    } else {
        struct itimerspec ts;
        ts.it_interval.tv_sec = 10;
        ts.it_interval.tv_nsec = 0;
        ts.it_value.tv_sec = 10;
        ts.it_value.tv_nsec = 0;

        if (timer_settime(timer_id, 0, &ts, NULL) != 0) {
            syslog(LOG_ERR, "Failure to arm timer!!!");
        }
    }
}

int main(int argc, char **argv) {
    int daemon = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon = 1;
    } 

    openlog(NULL, LOG_ODELAY, LOG_USER);

    server_fd = start_server(SERVER_PORT, daemon);
    if (server_fd < 0) {
        return EXIT_FAILURE;
    }

    if (register_sighandlers() != 0) {
        exit(EXIT_FAILURE);
    }

    syslog(LOG_DEBUG, "Value of flag - %d", USE_AESD_CHAR_DEVICE);
    
    // open and close data file
    int fd = open_datafile();
    if (fd < 0) {
        exit(EXIT_FAILURE);
    }
    close_datafile(fd);

    SLIST_INIT(&connections);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    if (!USE_AESD_CHAR_DEVICE) {
        create_timer();
    }

    for( ; stopApp < 1 ; ) {
        accept_conn();
    }

}