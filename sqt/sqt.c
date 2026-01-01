/*
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
        fprintf(stderr, "Erreur SQLite: %s\n", sqlite3_errmsg(conn->db));
        free(conn->filename);
        free(conn);
        return NULL;
    }
    
    conn->is_open = 1;
    printf("✅ Connecté à: %s\n", filename);
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
        fprintf(stderr, "Erreur SQL: %s\n", err_msg);
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
        fprintf(stderr, "Erreur préparation: %s\n", sqlite3_errmsg(conn->db));
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
        printf("Aucun résultat\n");
        return;
    }
    
    /* En-tête */
    for (int i = 0; i < result->column_count; i++) {
        printf("%-20s", result->column_names[i]);
    }
    printf("\n");
    
    for (int i = 0; i < result->column_count; i++) {
        printf("--------------------");
    }
    printf("\n");
    
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
        printf("\n");
    }
    printf("Total: %d lignes\n", result->row_count);
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
