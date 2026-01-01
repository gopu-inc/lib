


# ==========================================
#   Makefile pour zarch
# ==========================================

# Compilateur et options
CC      = gcc
CFLAGS  = -Wall -O2
LIBS    = -lcurl -ljansson

# Cibles
TARGET  = zarch
SRC     = zarch.c

# ------------------------------------------
# Compilation
# ------------------------------------------

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)   # LIGNE 21 - indente avec TAB

# ------------------------------------------
# Nettoyage
# ------------------------------------------

clean:
	rm -f $(TARGET)

# ------------------------------------------
# Installation
# ------------------------------------------

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/
	@echo "✅ zarch installé dans /usr/local/bin"

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	@echo "🗑️ zarch désinstallé de /usr/local/bin"

# ------------------------------------------
# Phony targets
# ------------------------------------------
.PHONY: clean install uninstall
