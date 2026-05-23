CC      ?= gcc
CFLAGS  ?= -O2 -Wall -W -pipe -D_REENTRANT
CFLAGS  += -I. -I/usr/include/libusb-1.0
LDLIBS   = -lusb-1.0 -lm

TARGET   = flirone-v4l2
SRC      = flirone-v4l2.c
DEPS     = plank.h font5x7.h

all: $(TARGET)

$(TARGET): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDLIBS)

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
