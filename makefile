# Use `make old_android=true` to compile on old android devices
TARGET=guessnum-stat
OBJS=guessnum.o fileio.o worker.o main.o
HEADERS=guessnum.h fileio.h worker.h random.h

ifdef old_android
CC=arm-linux-androideabi-gcc
CFLAGS=-O3
else
CC=gcc
CFLAGS= -g -O0 -pthread
endif

LDFLAGS=-g -O0

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

guessnum.o:guessnum.c $(HEADERS)

fileio.o:fileio.c $(HEADERS)

worker.o:worker.c $(HEADERS)

main.o:main.c $(HEADERS)

.PHONY: clean
clean:
	-rm $(TARGET) *.o

