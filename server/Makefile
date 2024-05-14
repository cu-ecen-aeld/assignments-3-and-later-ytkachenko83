ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
	CC ?= $(CROSS_COMPILE)gcc
endif

CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -lpthread -lrt
SRC := aesdsocket.c datafile.c
TARGET ?= aesdsocket
OBJS := $(SRC:.c=.o)

EXTRA_CFLAGS = -DUSE_AESD_CHAR_DEVICE=$(if $(USE_AESD_CHAR_DEVICE),$(USE_AESD_CHAR_DEVICE),1)

default_target: all

# Targets
all: $(TARGET)

$(OBJS) : CFLAGS += $(EXTRA_CFLAGS)
$(TARGET) : $(OBJS)	
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	$(RM) $(TARGET) $(OBJS)
