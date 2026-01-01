#!/usr/bin/env python3
"""
Créateur de package SQLT minimal - 5 fichiers seulement
"""

import os
from pathlib import Path

def create_sqt_simple():
    """Crée une structure SQLT minimale"""
    
    # 1. Créer le dossier principal
    package_dir = Path("sqt")
    package_dir.mkdir(exist_ok=True)
    print(f"📁 1. Dossier créé: {package_dir}")
    
    # 2. Fichier sqt.h (header C)
    sqt_h = """/*
 * SQLT - SQL Toolkit v1.0
 * Header file
 */

#ifndef SQLT_H
#define SQLT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

/* Types de données */
typedef enum {
    SQLT_INTEGER,
    SQLT_TEXT,
    SQLT_REAL,
    SQLT_BLOB,
    SQLT_NULL
} sqt_type_t;

/* Structure de connexion */
typedef struct {
    sqlite3 *db;
    char *filename;
    int is_open;
} sqt_connection_t;

/* Structure de requête */
typedef struct {
    sqt_connection_t *conn;
    sqlite3_stmt *stmt;
    char *sql;
    int param_count;
} sqt_query_t;

/* Structure de résultat */
typedef struct {
    int column_count;
    char **column_names;
    sqt_type_t *column_types;
    void ***rows;
    int row_count;
} sqt_result_t;

/* Fonctions principales */
sqt_connection_t* sqt_connect(const char *filename);
void sqt_disconnect(sqt_connection_t *conn);
int sqt_execute(sqt_connection_t *conn, const char *sql);
sqt_result_t* sqt_query(sqt_connection_t *conn, const char *sql, ...);
void sqt_free_result(sqt_result_t *result);
void sqt_print_result(sqt_result_t *result);

/* Fonctions utilitaires */
int sqt_begin_transaction(sqt_connection_t *conn);
int sqt_commit_transaction(sqt_connection_t *conn);
int sqt_rollback_transaction(sqt_connection_t *conn);
int sqt_table_exists(sqt_connection_t *conn, const char *table_name);

#endif /* SQLT_H */
"""
    (package_dir / "sqt.h").write_text(sqt_h)
    print(f"📄 2. Header créé: {package_dir}/sqt.h")
    
    # 3. Fichier sqt.c (source C)
    sqt_c = """/*
 * SQLT - SQL Toolkit v1.0
 * Implementation
 */

#include "sqt.h"
#include <stdarg.h>

/* Connexion à une base de données */
sqt_connection_t* sqt_connect(const char *filename) {
    sqt_connection_t *conn = malloc(sizeof(sqt_connection_t));
    if (!conn) return NULL;
    
    conn->filename = strdup(filename);
    conn->is_open = 0;
    
    int rc = sqlite3_open(filename, &conn->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erreur SQLite: %s\\n", sqlite3_errmsg(conn->db));
        free(conn->filename);
        free(conn);
        return NULL;
    }
    
    conn->is_open = 1;
    printf("✅ Connecté à: %s\\n", filename);
    return conn;
}

/* Ferme la connexion */
void sqt_disconnect(sqt_connection_t *conn) {
    if (!conn) return;
    
    if (conn->is_open && conn->db) {
        sqlite3_close(conn->db);
        conn->is_open = 0;
    }
    
    free(conn->filename);
    free(conn);
}

/* Exécute une commande SQL simple */
int sqt_execute(sqt_connection_t *conn, const char *sql) {
    if (!conn || !conn->is_open) return -1;
    
    char *err_msg = NULL;
    int rc = sqlite3_exec(conn->db, sql, NULL, NULL, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erreur SQL: %s\\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    return 0;
}

/* Exécute une requête avec paramètres */
sqt_result_t* sqt_query(sqt_connection_t *conn, const char *sql, ...) {
    if (!conn || !conn->is_open) return NULL;
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(conn->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Erreur préparation: %s\\n", sqlite3_errmsg(conn->db));
        return NULL;
    }
    
    /* Liaison des paramètres */
    va_list args;
    va_start(args, sql);
    int param_index = 1;
    
    while (1) {
        const char *value = va_arg(args, const char*);
        if (!value) break;
        
        sqlite3_bind_text(stmt, param_index, value, -1, SQLITE_TRANSIENT);
        param_index++;
    }
    va_end(args);
    
    /* Exécution et récupération des résultats */
    sqt_result_t *result = malloc(sizeof(sqt_result_t));
    if (!result) return NULL;
    
    result->column_count = sqlite3_column_count(stmt);
    result->row_count = 0;
    
    /* Noms des colonnes */
    result->column_names = malloc(result->column_count * sizeof(char*));
    for (int i = 0; i < result->column_count; i++) {
        result->column_names[i] = strdup(sqlite3_column_name(stmt, i));
    }
    
    /* Types des colonnes */
    result->column_types = malloc(result->column_count * sizeof(sqt_type_t));
    
    /* Récupération des lignes */
    int capacity = 10;
    result->rows = malloc(capacity * sizeof(void**));
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (result->row_count >= capacity) {
            capacity *= 2;
            result->rows = realloc(result->rows, capacity * sizeof(void**));
        }
        
        void **row = malloc(result->column_count * sizeof(void*));
        for (int i = 0; i < result->column_count; i++) {
            switch (sqlite3_column_type(stmt, i)) {
                case SQLITE_INTEGER:
                    result->column_types[i] = SQLT_INTEGER;
                    int *int_val = malloc(sizeof(int));
                    *int_val = sqlite3_column_int(stmt, i);
                    row[i] = int_val;
                    break;
                    
                case SQLITE_TEXT:
                    result->column_types[i] = SQLT_TEXT;
                    const char *text = (const char*)sqlite3_column_text(stmt, i);
                    row[i] = strdup(text ? text : "");
                    break;
                    
                case SQLITE_FLOAT:
                    result->column_types[i] = SQLT_REAL;
                    double *dbl_val = malloc(sizeof(double));
                    *dbl_val = sqlite3_column_double(stmt, i);
                    row[i] = dbl_val;
                    break;
                    
                default:
                    result->column_types[i] = SQLT_NULL;
                    row[i] = NULL;
            }
        }
        
        result->rows[result->row_count] = row;
        result->row_count++;
    }
    
    sqlite3_finalize(stmt);
    return result;
}

/* Libère les résultats */
void sqt_free_result(sqt_result_t *result) {
    if (!result) return;
    
    for (int i = 0; i < result->column_count; i++) {
        free(result->column_names[i]);
    }
    free(result->column_names);
    free(result->column_types);
    
    for (int r = 0; r < result->row_count; r++) {
        void **row = result->rows[r];
        for (int c = 0; c < result->column_count; c++) {
            if (row[c]) free(row[c]);
        }
        free(row);
    }
    free(result->rows);
    free(result);
}

/* Affiche les résultats */
void sqt_print_result(sqt_result_t *result) {
    if (!result || result->row_count == 0) {
        printf("Aucun résultat\\n");
        return;
    }
    
    /* En-tête */
    for (int i = 0; i < result->column_count; i++) {
        printf("%-20s", result->column_names[i]);
    }
    printf("\\n");
    
    for (int i = 0; i < result->column_count; i++) {
        printf("--------------------");
    }
    printf("\\n");
    
    /* Données */
    for (int r = 0; r < result->row_count; r++) {
        void **row = result->rows[r];
        for (int c = 0; c < result->column_count; c++) {
            if (!row[c]) {
                printf("%-20s", "NULL");
            } else {
                switch (result->column_types[c]) {
                    case SQLT_INTEGER:
                        printf("%-20d", *(int*)row[c]);
                        break;
                    case SQLT_TEXT:
                        printf("%-20s", (char*)row[c]);
                        break;
                    case SQLT_REAL:
                        printf("%-20.2f", *(double*)row[c]);
                        break;
                    default:
                        printf("%-20s", "?");
                }
            }
        }
        printf("\\n");
    }
    printf("Total: %d lignes\\n", result->row_count);
}

/* Gestion des transactions */
int sqt_begin_transaction(sqt_connection_t *conn) {
    return sqt_execute(conn, "BEGIN TRANSACTION");
}

int sqt_commit_transaction(sqt_connection_t *conn) {
    return sqt_execute(conn, "COMMIT");
}

int sqt_rollback_transaction(sqt_connection_t *conn) {
    return sqt_execute(conn, "ROLLBACK");
}

/* Vérifie si une table existe */
int sqt_table_exists(sqt_connection_t *conn, const char *table_name) {
    char sql[256];
    snprintf(sql, sizeof(sql), 
             "SELECT name FROM sqlite_master WHERE type='table' AND name='%s'", 
             table_name);
    
    sqt_result_t *result = sqt_query(conn, sql, NULL);
    if (!result) return 0;
    
    int exists = (result->row_count > 0);
    sqt_free_result(result);
    
    return exists;
}
"""
    (package_dir / "sqt.c").write_text(sqt_c)
    print(f"📄 3. Source créé: {package_dir}/sqt.c")
    
    # 4. Makefile
    makefile = """# Makefile pour SQLT
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LIBS = -lsqlite3
TARGET = libsqt.a
SO_TARGET = libsqt.so
EXAMPLES = example test_sqt

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

# Exemple simple
example: example.c $(TARGET)
	$(CC) $(CFLAGS) -o $@ $< -L. -lsqt $(LIBS)
	@echo "✅ Exemple compilé: $@"

# Programme de test
test_sqt: test_sqt.c $(TARGET)
	$(CC) $(CFLAGS) -o $@ $< -L. -lsqt $(LIBS)
	@echo "✅ Test compilé: $@"

# Nettoyage
clean:
	rm -f $(OBJS) $(TARGET) $(SO_TARGET) $(EXAMPLES) *.db
	@echo "🧹 Nettoyage terminé"

# Installation système
install: $(TARGET) $(SO_TARGET)
	cp sqt.h /usr/local/include/
	cp $(TARGET) $(SO_TARGET) /usr/local/lib/
	ldconfig
	@echo "📦 SQLT installé système"

# Désinstallation
uninstall:
	rm -f /usr/local/include/sqt.h
	rm -f /usr/local/lib/libsqt.*
	@echo "🗑️ SQLT désinstallé"

# Tests rapides
test: test_sqt
	./test_sqt

.PHONY: all clean install uninstall test
"""
    (package_dir / "Makefile").write_text(makefile)
    print(f"📄 4. Makefile créé: {package_dir}/Makefile")
    
    # 5. Fichier d'exemple
    example_c = """/*
 * Exemple d'utilisation de SQLT
 */

#include "sqt.h"
#include <stdio.h>

int main() {
    printf("=== SQLT Example ===\\n");
    
    // 1. Connexion
    sqt_connection_t *conn = sqt_connect("test.db");
    if (!conn) {
        printf("❌ Erreur de connexion\\n");
        return 1;
    }
    
    // 2. Création de table
    printf("\\n1. Création de table...\\n");
    sqt_execute(conn, 
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "age INTEGER, "
        "email TEXT UNIQUE)"
    );
    
    // 3. Insertion de données
    printf("\\n2. Insertion de données...\\n");
    sqt_begin_transaction(conn);
    
    sqt_execute(conn, 
        "INSERT INTO users (name, age, email) VALUES "
        "('Alice', 25, 'alice@example.com')"
    );
    
    sqt_execute(conn,
        "INSERT INTO users (name, age, email) VALUES "
        "('Bob', 30, 'bob@example.com')"
    );
    
    sqt_execute(conn,
        "INSERT INTO users (name, age, email) VALUES "
        "('Charlie', 35, 'charlie@example.com')"
    );
    
    sqt_commit_transaction(conn);
    
    // 4. Requête simple
    printf("\\n3. Requête simple...\\n");
    sqt_result_t *result = sqt_query(conn, 
        "SELECT id, name, age, email FROM users WHERE age > 26", NULL);
    
    if (result) {
        sqt_print_result(result);
        sqt_free_result(result);
    }
    
    // 5. Requête paramétrée
    printf("\\n4. Requête paramétrée...\\n");
    result = sqt_query(conn,
        "SELECT * FROM users WHERE name LIKE ?", "%b%", NULL);
    
    if (result) {
        sqt_print_result(result);
        sqt_free_result(result);
    }
    
    // 6. Vérification de table
    printf("\\n5. Vérifications...\\n");
    if (sqt_table_exists(conn, "users")) {
        printf("✅ Table 'users' existe\\n");
    }
    
    // 7. Nettoyage
    sqt_execute(conn, "DELETE FROM users");
    printf("\\n6. Table vidée\\n");
    
    // 8. Fermeture
    sqt_disconnect(conn);
    printf("\\n✅ Exemple terminé\\n");
    
    return 0;
}
"""
    (package_dir / "example.c").write_text(example_c)
    print(f"📄 5. Exemple créé: {package_dir}/example.c")
    
    print(f"\\n✅ Structure SQLT créée avec 5 fichiers dans: {package_dir}")
    print("\\nPour compiler:")
    print("  cd sqt")
    print("  make")
    print("  ./example")
    print("\\nPour installer:")
    print("  sudo make install")

if __name__ == "__main__":
    create_sqt_simple()