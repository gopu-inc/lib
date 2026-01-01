/*
 * Exemple d'utilisation de SQLT
 */

#include "sqt.h"
#include <stdio.h>

int main() {
    printf("=== SQLT Example ===\n");
    
    // 1. Connexion
    sqt_connection_t *conn = sqt_connect("test.db");
    if (!conn) {
        printf("❌ Erreur de connexion\n");
        return 1;
    }
    
    // 2. Création de table
    printf("\n1. Création de table...\n");
    sqt_execute(conn, 
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "age INTEGER, "
        "email TEXT UNIQUE)"
    );
    
    // 3. Insertion de données
    printf("\n2. Insertion de données...\n");
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
    printf("\n3. Requête simple...\n");
    sqt_result_t *result = sqt_query(conn, 
        "SELECT id, name, age, email FROM users WHERE age > 26", NULL);
    
    if (result) {
        sqt_print_result(result);
        sqt_free_result(result);
    }
    
    // 5. Requête paramétrée
    printf("\n4. Requête paramétrée...\n");
    result = sqt_query(conn,
        "SELECT * FROM users WHERE name LIKE ?", "%b%", NULL);
    
    if (result) {
        sqt_print_result(result);
        sqt_free_result(result);
    }
    
    // 6. Vérification de table
    printf("\n5. Vérifications...\n");
    if (sqt_table_exists(conn, "users")) {
        printf("✅ Table 'users' existe\n");
    }
    
    // 7. Nettoyage
    sqt_execute(conn, "DELETE FROM users");
    printf("\n6. Table vidée\n");
    
    // 8. Fermeture
    sqt_disconnect(conn);
    printf("\n✅ Exemple terminé\n");
    
    return 0;
}
