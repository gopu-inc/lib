# Makefile for zarch
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LIBS = -lcurl -ljansson
TARGET = zarch
SRC = zarch.c

all: $(TARGET)

$(TARGET): $(SRC)
$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

install: $(TARGET)
cp $(TARGET) /usr/local/bin/
chmod +x /usr/local/bin/$(TARGET)

clean:
rm -f $(TARGET) *.o

.PHONY: all install clean
