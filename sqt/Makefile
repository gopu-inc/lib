# Makefile pour SQLT (Alpine Linux)
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -fPIC
LIBS = -lsqlite3
TARGET = libsqt.a
SO_TARGET = libsqt.so
EXAMPLES = example

# Fichiers source
SRCS = sqt.c
OBJS = $(SRCS:.c=.o)

# Cible par défaut
all: $(TARGET) $(SO_TARGET) $(EXAMPLES)

# Bibliothèque statique
$(TARGET): $(OBJS)
	ar rcs $@ $(OBJS)
	@echo "✅ Bibliothèque statique créée: $@"

# Bibliothèque dynamique
$(SO_TARGET): $(OBJS)
	$(CC) -shared -o $@ $(OBJS) $(LIBS)
	@echo "✅ Bibliothèque dynamique créée: $@"

# Compilation des objets
%.o: %.c sqt.h
	$(CC) $(CFLAGS) -c $< -o $@

# Exemple
example: example.c $(TARGET)
	$(CC) $(CFLAGS) -o $@ $< -L. -lsqt $(LIBS)
	@echo "✅ Exemple compilé: $@"

# Nettoyage
clean:
	rm -f $(OBJS) $(TARGET) $(SO_TARGET) $(EXAMPLES) *.db
	@echo "🧹 Nettoyage terminé"

# Installation système (sans ldconfig)
install: $(TARGET) $(SO_TARGET)
	cp sqt.h /usr/local/include/
	cp $(TARGET) $(SO_TARGET) /usr/local/lib/
	@echo "📦 SQLT installé dans /usr/local/"
	@echo "⚠️  Note: ldconfig non exécuté (Alpine Linux)"

# Installation manuelle pour Alpine
install-alpine: $(TARGET) $(SO_TARGET)
	cp sqt.h /usr/include/
	cp $(TARGET) $(SO_TARGET) /usr/lib/
	@echo "📦 SQLT installé dans /usr/ (Alpine)"

# Désinstallation
uninstall:
	rm -f /usr/local/include/sqt.h
	rm -f /usr/local/lib/libsqt.*
	rm -f /usr/include/sqt.h 2>/dev/null || true
	rm -f /usr/lib/libsqt.* 2>/dev/null || true
	@echo "🗑️ SQLT désinstallé"

# Tests rapides
test: example
	./example

.PHONY: all clean install install-alpine uninstall test