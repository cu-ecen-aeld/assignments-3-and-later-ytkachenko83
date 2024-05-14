#include <stdio.h>

#ifndef USE_AESD_CHAR_DEVICE
#   define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE == 1
#   define DATA_FILE_PATH "/dev/aesdchar"
#else
#   define DATA_FILE_PATH "/var/tmp/aesdsocketdata"
#endif

int open_datafile();
void close_datafile(int fd);
int adjust_datafile_pos(int fd, int offset, int pos_kind);
void append_datafile(int fd, char *buf, int size);
void append_string(char **buf, size_t* maxbuflen, size_t buflen, char *in_buf, int n);