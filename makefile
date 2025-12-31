# Makefile for zarch with curl support
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LIBS = -lcurl
TARGET = zarch
SRC = zarch.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

install: $(TARGET)
	@echo "Installing zarch..."
	cp $(TARGET) /usr/local/bin/
	chmod +x /usr/local/bin/$(TARGET)
	@echo "✅ zarch installed to /usr/local/bin/"

install-deps:
	@echo "Installing dependencies..."
	apk add curl-dev
	@echo "✅ Dependencies installed"

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	@echo "✅ zarch uninstalled"

clean:
	rm -f $(TARGET) *.o
	@echo "✅ Cleaned build files"

test: $(TARGET)
	@echo "Testing zarch..."
	./$(TARGET) list
	./$(TARGET) hub-status

.PHONY: all install install-deps uninstall clean test
