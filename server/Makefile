ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
	CC = $(CROSS_COMPILE)gcc
endif

CFLAGS = -g -Wall -Werror -pthread
LDFLAGS = -lrt
SRC := aesdsocket.c datafile.c
TARGET = aesdsocket
OBJS := $(SRC:.c=.o)

default_target: all

# Targets
all: $(TARGET)

$(TARGET) : $(OBJS)	
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET)