#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

int exitWriter(int code) {
    closelog();

    return code;
}

int main(int argc, char** argv) {
    openlog(NULL, LOG_ODELAY, LOG_USER);

    if (argc <= 2) {
        printf("Usage: write <FILE> <STRING>\n");

        return exitWriter(1);
    }

    const char* filename = argv[1];
    const char* text = argv[2];

    if (strlen(filename) == 0) {
        fprintf(stderr, "Required parameter 'FILE' is blank.\n");

        return exitWriter(1);
    }
    if (strlen(text) == 0) {
        fprintf(stderr, "Required parameter 'STRING' is blank.\n");

        return exitWriter(1);
    }

    syslog(LOG_DEBUG, "Writing <%s> to <%s>", text, filename);

    int exitCode = 0;
    FILE* f = fopen(filename, "w");
    if (f != NULL) {
        size_t result = fwrite(text, strlen(text), 1, f);
        if (result == 0) {
            syslog(LOG_ERR, "Failure to write file: %s", strerror(errno));
            exitCode = 1;
        }

        fclose(f);
    } else {
        syslog(LOG_ERR, "Failure to write file: %s", strerror(errno));
    }

    return exitWriter(exitCode);
}