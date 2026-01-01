/*
 * SQLT CLI - Command Line Interface for SQLT
 * v1.1.0
 */
#include "sqt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void show_help() {
    printf("SQLT CLI v1.1.0 - SQL Toolkit Command Line\n");
    printf("Usage: sqt-cli <database> [command] [args]\n\n");
    printf("Commands:\n");
    printf("  init                 Initialize new database\n");
    printf("  tables               List all tables\n");
    printf("  query \"SQL\"         Execute SQL query\n");
    printf("  exec \"SQL\"          Execute SQL command\n");
    printf("  import <file>        Import SQL from file\n");
    printf("  dump                 Dump database schema\n");
    printf("  help                 Show this help\n\n");
    printf("Examples:\n");
    printf("  sqt-cli test.db init\n");
    printf("  sqt-cli test.db tables\n");
    printf("  sqt-cli test.db query \"SELECT * FROM users\"\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        show_help();
        return 1;
    }
    
    char *db_file = argv[1];
    
    if (strcmp(db_file, "help") == 0 || strcmp(db_file, "--help") == 0) {
        show_help();
        return 0;
    }
    
    // Connect to database
    sqt_connection_t *conn = sqt_connect(db_file);
    if (!conn) {
        printf("❌ Cannot connect to database: %s\n", db_file);
        return 1;
    }
    
    printf("✅ Connected to: %s\n", db_file);
    
    // Process commands
    if (argc == 2) {
        // No command, just connect
        printf("Use 'sqt-cli %s help' for available commands\n", db_file);
    } else if (argc >= 3) {
        char *command = argv[2];
        
        if (strcmp(command, "init") == 0) {
            printf("📦 Initializing database...\n");
            // Create example table
            sqt_execute(conn, 
                "CREATE TABLE IF NOT EXISTS sqt_info ("
                "id INTEGER PRIMARY KEY,"
                "version TEXT,"
                "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");
            printf("✅ Database initialized\n");
            
        } else if (strcmp(command, "tables") == 0) {
            printf("📋 Database tables:\n");
            sqt_result_t *result = sqt_query(conn,
                "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name", NULL);
            if (result) {
                sqt_print_result(result);
                sqt_free_result(result);
            }
            
        } else if (strcmp(command, "query") == 0 && argc >= 4) {
            char *sql = argv[3];
            printf("🔍 Executing query: %s\n", sql);
            sqt_result_t *result = sqt_query(conn, sql, NULL);
            if (result) {
                sqt_print_result(result);
                sqt_free_result(result);
            } else {
                printf("❌ Query failed\n");
            }
            
        } else if (strcmp(command, "exec") == 0 && argc >= 4) {
            char *sql = argv[3];
            printf("⚡ Executing: %s\n", sql);
            if (sqt_execute(conn, sql) == 0) {
                printf("✅ Command executed successfully\n");
            } else {
                printf("❌ Command failed\n");
            }
            
        } else if (strcmp(command, "dump") == 0) {
            printf("📄 Database schema:\n");
            sqt_result_t *result = sqt_query(conn,
                "SELECT sql FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'", NULL);
            if (result) {
                for (int i = 0; i < result->row_count; i++) {
                    printf("%s;\n", (char*)result->rows[i][0]);
                }
                sqt_free_result(result);
            }
            
        } else if (strcmp(command, "help") == 0) {
            show_help();
            
        } else {
            printf("❌ Unknown command: %s\n", command);
            printf("💡 Try: sqt-cli %s help\n", db_file);
        }
    }
    
    // Close connection
    sqt_disconnect(conn);
    printf("👋 Disconnected\n");
    
    return 0;
}