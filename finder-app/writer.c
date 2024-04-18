#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int exitWriter(int code) {
    closelog();

    return code;
}

int main(int argc, char** argv) {
    openlog(NULL, LOG_ODELAY, LOG_USER);

    if (argc <= 2) {
        syslog(LOG_ERR, "Usage: write <FILE> <STRING>");

        return exitWriter(1);
    }

    const char* filename = argv[1];
    const char* text = argv[2];

    if (strlen(filename) == 0) {
        syslog(LOG_ERR, "Required parameter 'FILE' is blank.");

        return exitWriter(1);
    }
    if (strlen(text) == 0) {
        syslog(LOG_ERR, "Required parameter 'STRING' is blank.");

        return exitWriter(1);
    }

    syslog(LOG_DEBUG, "Writing <%s> to <%s>", text, filename);

    int exitCode = 0;
    int fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd > 0) {
        ssize_t result = write(fd, text, strlen(text));
        if (result == -1) {
            syslog(LOG_ERR, "Failure to write file: %s", strerror(errno));
            exitCode = 1;
        }

        close(fd);
    } else {
        syslog(LOG_ERR, "Failure to write file: %s", strerror(errno));
    }

    return exitWriter(exitCode);
}