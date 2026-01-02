CC = gcc
CFLAGS = -Wall -O2 -D_DEFAULT_SOURCE
LIBS = -lcurl -ljansson -lcrypto -lz
TARGET = zarch
SOURCES = zarch.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LIBS)

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(TARGET)
	mkdir -p ~/.zarch/cache

clean:
	rm -f $(TARGET)

uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)
	rm -rf ~/.zarch

test:
	./$(TARGET) --help

.PHONY: all install clean uninstall test
