/*
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
