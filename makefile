# Makefile for zarch
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
TARGET = zarch
SRC = zarch.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	@echo "Installing zarch..."
	cp $(TARGET) /usr/local/bin/
	chmod +x /usr/local/bin/$(TARGET)
	@echo "✅ zarch installed to /usr/local/bin/"

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	@echo "✅ zarch uninstalled"

clean:
	rm -f $(TARGET) *.o
	@echo "✅ Cleaned build files"

test: $(TARGET)
	@echo "Testing zarch..."
	./$(TARGET) list
	./$(TARGET) help

.PHONY: all install uninstall clean test
