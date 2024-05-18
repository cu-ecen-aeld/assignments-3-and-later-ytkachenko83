#include "datafile.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

#if USE_AESD_CHAR_DEVICE == 1
#include <sys/ioctl.h>
#include "../aesd-char-driver/aesd_ioctl.h"
#endif

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

int parse_seekto_cmd(char *buf, int size, struct aesd_seekto *seekto) {
    const char *match_cmd = "AESDCHAR_IOCSEEKTO:";
    char *cmd;

    cmd = strstr(buf, match_cmd);
    if (cmd) {
        int i, j = 0, seen_comma = 0;
        uint32_t x, y;
        char digits[1024];
        memset(digits, 0, sizeof(digits));

        for (i = strlen(match_cmd); i < size; i++) {
            if (!seen_comma && cmd[i] == ',') {
                seen_comma = 1;
                j = 0;
                errno = 0;
                x = strtoul(digits, NULL, 10);
                if (errno != 0) {
                    return -1;
                }
                memset(digits, 0, sizeof(digits));
            } else {
                digits[j++] = cmd[i];
            }
        }
        if (strlen(digits) > 0) {
            errno = 0;
            y = strtoul(digits, NULL, 10);
            if (errno != 0) {
                return -1;    
            }
        }
        seekto->write_cmd = x;
        seekto->write_cmd_offset = y;
        
        return 0;
    }

    return -1;
}


void append_datafile(int fd, char *buf, int size) {
#if USE_AESD_CHAR_DEVICE == 1
    struct aesd_seekto seekto;
    memset(&seekto, 0, sizeof(seekto));

    if (parse_seekto_cmd(buf, size, &seekto) != -1) {
        if (ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto) != 0) {
            syslog(LOG_ERR, "Failure to write to the datafile: %s", strerror(errno));
        }
    } else 
#endif    
    if (write(fd, buf, size) == -1) {
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
