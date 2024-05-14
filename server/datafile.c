#include "datafile.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

int open_datafile() {
    int fd = open(DATA_FILE_PATH, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    if (fd < 0) {
        syslog(LOG_ERR, "Failure to open/create file - %s: %s", DATA_FILE_PATH, strerror(errno));
    }

    return fd;
}

void close_datafile(int fd) {
    if (fd > 0) {
        int rc = close(fd);
        if (rc < 0) {
            syslog(LOG_ERR, "Failure to close file - %s: %s", DATA_FILE_PATH, strerror(errno));
        } 
    }
}

void destroy_datafile() {
    if (!USE_AESD_CHAR_DEVICE) {
        remove(DATA_FILE_PATH);
    }
}

int adjust_datafile_pos(int fd, int offset, int pos_kind) {
#if USE_AESD_CHAR_DEVICE == 0
    // put cursor to the end of file
    int rc = lseek(fd, offset, pos_kind);
    if (rc == -1) {
        syslog(LOG_ERR, "Failure to reposition cursor file to the EOF: %s", strerror(errno));
    }
#else
    int rc = 0;
#endif    
    return rc;
}


void append_datafile(int fd, char *buf, int size) {
    int rc = write(fd, buf, size);
    if (rc == -1) {
        syslog(LOG_ERR, "Failure to write to the datafile: %s", strerror(errno));
    }
}

void append_string(char **buf, size_t* maxbuflen, size_t buflen, char *in_buf, int n) {
    if (*maxbuflen < buflen + n + 1) {
        *maxbuflen += buflen + 2*n;
        *buf = realloc(*buf, *maxbuflen);
    } 
    strncat(*buf, in_buf, n);
}
