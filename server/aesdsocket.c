#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>

#define SERVER_PORT "9000"
#define LISTEN_BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define NEWLINE '\n'
#define BUFFER_SIZE 128

int server_fd, client_fd, data_fd;
char **buf;
size_t maxbuflen = BUFFER_SIZE;
volatile sig_atomic_t stopApp;

void close_datafile(int fd);

void cleanup() {
    close_datafile(data_fd);
    if (server_fd > 0) {
        close(server_fd);
    }

    free(*buf);
    free(buf);

    closelog();
}

static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        stopApp = 1;
        syslog(LOG_DEBUG, "%s", "Caught signal, exiting");

        free(*buf);
        free(buf);
        
        if (client_fd > 0) {
            close(client_fd);
        }
        if (server_fd > 0) {
            while (shutdown(server_fd, SHUT_RDWR) == -1) {
                syslog(LOG_ERR, "Server socket shutdown error: %s", strerror(errno));
            }
            close(server_fd);
        }
        close_datafile(data_fd);
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

int open_datafile() {
    int fd = open(DATA_FILE, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    if (fd < 0) {
        syslog(LOG_ERR, "Failure to open/create file - %s: %s", DATA_FILE, strerror(errno));
    }

    return fd;
}

void close_datafile(int fd) {
    if (fd > 0) {
        int rc = close(fd);
        if (rc < 0) {
            syslog(LOG_ERR, "Failure to close file - %s: %s", DATA_FILE, strerror(errno));
        } else {
            remove(DATA_FILE);
        }
    }
}

int adjust_datafile_pos(int fd, int offset, int pos_kind) {
    // put cursor to the end of file
    int rc = lseek(fd, offset, pos_kind);
    if (rc == -1) {
        syslog(LOG_ERR, "Failure to reposition cursor file to the EOF: %s", strerror(errno));
    }

    return rc;
}

void append_datafile(int fd, char *buf, int size) {
    int rc = write(fd, buf, size);
    if (rc == -1) {
        syslog(LOG_ERR, "Failure to write to the datafile: %s", strerror(errno));
    }
}

int send_response(int data_fd, int client_fd) {
    char readbuf[BUFFER_SIZE];
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

void append_string(char **buf, size_t buflen, char *in_buf, int n) {
    if (maxbuflen < buflen + n + 1) {
        maxbuflen += buflen + 2*n;
        *buf = realloc(*buf, maxbuflen);
    } 
    strncat(*buf, in_buf, n);
}

void accept_conn() {
    struct sockaddr client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    client_fd = accept(server_fd, &client_addr, &client_addr_len);
    if (client_fd == -1) {
        syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
        return;
    }
    // get client ip
    struct sockaddr_in *client_inaddr = (struct sockaddr_in *) &client_addr;
    char *client_ip_addr = inet_ntoa(client_inaddr->sin_addr);

    syslog(LOG_DEBUG, "Accepted connection from %s", client_ip_addr);

    if (adjust_datafile_pos(data_fd, 0, SEEK_END) > -1) { // position to EOF

        //read client data
        char recv_buf[BUFFER_SIZE];
        int n;
        size_t buflen = 0;
        *buf[0] = '\0';

        while((n = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {
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

            append_string(buf, buflen, recv_buf, currlen);
            buflen += currlen;

            if (has_nl) {
                // append to the data file
                append_datafile(data_fd, *buf, strlen(*buf));
                *buf[0] = '\0'; // make string empty!
                buflen = 0;

                int rc = send_response(data_fd, client_fd);
                if (rc < 0) {
                    syslog(LOG_ERR, "Failure to send response to the client - %s", client_ip_addr);
                    break;
                }
                // data file position will be EOF and we can copy any bytes after NEWLINE
                if (k < n) {
                    buflen = n-k-1;
                    append_string(buf, buflen, &recv_buf[k+1], buflen);
                }
            }
        }
    }

    close(client_fd);
    syslog(LOG_DEBUG, "Closed connection from %s", client_ip_addr);
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

int main(int argc, char **argv) {
    buf = malloc(sizeof(char *));
    *buf = malloc(maxbuflen);

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
    
    // start data file
    data_fd = open_datafile();
    if (data_fd < 0) {
        exit(EXIT_FAILURE);
    }

    for( ; stopApp < 1 ; ) {
        accept_conn();
    }

}