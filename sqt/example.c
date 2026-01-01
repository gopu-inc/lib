/*
 * SQLT v1.1.0 - Example program
 */
#include "sqt.h"
#include <stdio.h>

int main() {
    printf("=== SQLT v1.1.0 Example ===\n");
    
    // Create database
    sqt_connection_t *conn = sqt_connect("example.db");
    if (!conn) {
        printf("Failed to connect\n");
        return 1;
    }
    
    printf("✅ Connected to database\n");
    
    // Create table
    sqt_execute(conn,
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "email TEXT UNIQUE)");
    
    printf("📊 Table 'users' created\n");
    
    // Insert data
    sqt_begin_transaction(conn);
    
    sqt_execute(conn, "INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')");
    sqt_execute(conn, "INSERT INTO users (name, email) VALUES ('Bob', 'bob@example.com')");
    sqt_execute(conn, "INSERT INTO users (name, email) VALUES ('Charlie', 'charlie@example.com')");
    
    sqt_commit_transaction(conn);
    
    printf("📝 3 users inserted\n");
    
    // Query data
    printf("\n🔍 All users:\n");
    sqt_result_t *result = sqt_query(conn,
        "SELECT id, name, email FROM users ORDER BY name", NULL);
    
    if (result) {
        sqt_print_result(result);
        sqt_free_result(result);
    }
    
    // Cleanup
    sqt_execute(conn, "DELETE FROM users");
    printf("\n🧹 Table cleared\n");
    
    // Check table exists
    if (sqt_table_exists(conn, "users")) {
        printf("✅ Table verification passed\n");
    }
    
    sqt_disconnect(conn);
    printf("\n🎉 Example completed successfully!\n");
    
    return 0;
}
