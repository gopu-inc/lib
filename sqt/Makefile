# ==========================================
#   Makefile for SQLT v1.1.0
# ==========================================

# Compilateur et options
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99
LIBS    = -lsqlite3

# Cibles principales
TARGETS = libsqt.a example sqt-cli

# ------------------------------------------
# Règles de construction
# ------------------------------------------

all: $(TARGETS)

# Bibliothèque statique
libsqt.a: sqt.o
ar rcs $@ $^

# Exemple d’utilisation
example: example.c sqt.o
$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# CLI principale
sqt-cli: sqt-cli.c sqt.o
$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Objet principal
sqt.o: sqt.c sqt.h
$(CC) $(CFLAGS) -c $< -o $@

# ------------------------------------------
# Installation
# ------------------------------------------

install: all
cp sqt.h     /usr/local/include/
cp libsqt.a  /usr/local/lib/
cp sqt-cli   /usr/local/bin/
@echo "✅ SQLT v1.1.0 installed"

# ------------------------------------------
# Nettoyage
# ------------------------------------------

clean:
rm -f $(TARGETS) *.o *.db
@echo "🧹 Cleaned"

# ------------------------------------------
# Tests
# ------------------------------------------

test: example sqt-cli
./example
./sqt-cli example.db init

# ------------------------------------------
# Phony targets
# ------------------------------------------
.PHONY: all install clean test
