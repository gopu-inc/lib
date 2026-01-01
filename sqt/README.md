# SQLT - SQL Toolkit

SQLT est un toolkit SQL léger et puissant écrit en C, inspiré de SQLAlchemy. 
Il fournit une API simple pour interagir avec les bases de données SQLite.

## ✨ Fonctionnalités

- ✅ Connexion simple aux bases SQLite
- ✅ Exécution de requêtes SQL
- ✅ Requêtes paramétrées (prévention des injections)
- ✅ Gestion complète des transactions
- ✅ Récupération structurée des résultats
- ✅ Support des types de données SQL (INTEGER, TEXT, REAL, BLOB, NULL)
- ✅ Vérification d'existence des tables
- ✅ Affichage formaté des résultats

## 📦 Installation

### Compilation
```bash
# Cloner ou copier les fichiers
cd sqt

# Compiler avec make
make

# Ou compiler manuellement
gcc -Wall -Wextra -O2 -std=c99 -c sqt.c -o sqt.o
gcc -Wall -Wextra -O2 -std=c99 example.c sqt.o -o example -lsqlite3
```

Installation système (optionnel)

```bash
sudo make install
```

🚀 Utilisation rapide

```c
#include "sqt.h"
#include <stdio.h>

int main() {
    // Connexion à une base
    sqt_connection_t *conn = sqt_connect("ma_base.db");
    
    // Création de table
    sqt_execute(conn, 
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "email TEXT UNIQUE)");
    
    // Insertion de données
    sqt_execute(conn,
        "INSERT INTO users (name, email) VALUES "
        "('Alice', 'alice@example.com')");
    
    // Requête avec résultats
    sqt_result_t *result = sqt_query(conn,
        "SELECT * FROM users WHERE name LIKE ?", "%Ali%", NULL);
    
    if (result) {
        sqt_print_result(result);  // Affichage formaté
        sqt_free_result(result);   // Libération mémoire
    }
    
    // Fermeture
    sqt_disconnect(conn);
    return 0;
}
```

📁 Structure du projet

```
sqt/
├── sqt.h           # Header avec toutes les déclarations
├── sqt.c           # Implémentation complète
├── example.c       # Programme d'exemple
├── Makefile        # Script de compilation
└── README          # Ce fichier
```

🔧 API de base

Gestion des connexions

```c
sqt_connection_t* sqt_connect(const char *filename);
void sqt_disconnect(sqt_connection_t *conn);
```

Exécution de requêtes

```c
int sqt_execute(sqt_connection_t *conn, const char *sql);
sqt_result_t* sqt_query(sqt_connection_t *conn, const char *sql, ...);
void sqt_free_result(sqt_result_t *result);
```

Transactions

```c
int sqt_begin_transaction(sqt_connection_t *conn);
int sqt_commit_transaction(sqt_connection_t *conn);
int sqt_rollback_transaction(sqt_connection_t *conn);
```

Utilitaires

```c
int sqt_table_exists(sqt_connection_t *conn, const char *table_name);
void sqt_print_result(sqt_result_t *result);
```

📊 Types de données supportés

Type SQLT Type C Description
SQLT_INTEGER int Entiers 32-bit
SQLT_TEXT char* Chaînes de caractères
SQLT_REAL double Nombres à virgule flottante
SQLT_BLOB void* Données binaires
SQLT_NULL NULL Valeur nulle

🔒 Sécurité

· Requêtes paramétrées : Prévention des injections SQL
· Gestion mémoire : Libération automatique des ressources
· Transactions : Atomicité des opérations
· Validation : Vérification des erreurs SQLite

🧪 Tests

```bash
# Compiler et exécuter les tests
make
./example

# Résultat attendu :
# ✅ Connecté à: test.db
# ✅ Table créée
# ✅ Données insérées
# ✅ Résultats affichés
```

📦 Création de package pour zarch

```json
{
    "name": "sqt",
    "version": "1.0.0",
    "author": "Votre Nom",
    "license": "MIT",
    "description": "SQL Toolkit léger en C pour SQLite",
    "build_dir": ".",
    "output": "sqt-1.0.0.zv",
    "include": ["sqt.h", "sqt.c", "example.c", "Makefile", "README"],
    "exclude": ["*.db", "*.o", "*.so", "*.a"]
}
```

🏗️ Compilation croisée

```bash
# Pour Linux x86_64
gcc -Wall -O2 -std=c99 -m64 sqt.c example.c -o sqt_linux_x64 -lsqlite3

# Pour Linux ARM
arm-linux-gnueabi-gcc -Wall -O2 -std=c99 sqt.c example.c -o sqt_linux_arm -lsqlite3
```

🤝 Contribution

1. Fork le projet
2. Créez une branche (git checkout -b feature/amazing)
3. Committez vos changements (git commit -m 'Add amazing feature')
4. Push vers la branche (git push origin feature/amazing)
5. Ouvrez une Pull Request

📄 Licence

MIT License - voir le fichier LICENSE pour plus de détails.

🙏 Remerciements

· SQLite pour une base de données incroyablement légère
· SQLAlchemy pour l'inspiration de l'API
· Tous les contributeurs

📞 Support

Pour les questions et le support :

· Créez une issue sur GitHub
· Contactez l'auteur principal

---