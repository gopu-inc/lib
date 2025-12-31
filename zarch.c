#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Structure pour un paquet
typedef struct {
    char name[20];
    char install_cmd[100];
    char description[50];
} Package;

// Liste des paquets disponibles
Package packages[] = {
    {"git", "apk add git", "Version control system"},
    {"python3", "apk add python3 py3-pip", "Python 3 with pip"},
    {"wget", "apk add wget", "Web downloader"},
    {"curl", "apk add curl", "URL transfer tool"},
    {"vim", "apk add vim", "Text editor"},
    {"nano", "apk add nano", "Simple editor"},
    {"nodejs", "apk add nodejs npm", "JavaScript runtime"},
    {"gcc", "apk add build-base", "C compiler"},
    {"make", "apk add make", "Build tool"},
    {"bash", "apk add bash", "Bash shell"},
    {"htop", "apk add htop", "System monitor"},
    {"tmux", "apk add tmux", "Terminal multiplexer"},
    {"openssh", "apk add openssh-client", "SSH client"},
    {"rsync", "apk add rsync", "File sync tool"},
    {"tar", "apk add tar", "Archive utility"},
    {"gzip", "apk add gzip", "Compression tool"},
    {"zip", "apk add zip", "ZIP compression"},
    {"unzip", "apk add unzip", "ZIP extraction"}
};

int package_count = sizeof(packages) / sizeof(Package);

// Fonction pour exécuter une commande
int run_command(const char *cmd) {
    return system(cmd);
}

// Fonction pour installer un paquet
void install_package(const char *pkg_name) {
    int found = 0;
    
    for (int i = 0; i < package_count; i++) {
        if (strcmp(packages[i].name, pkg_name) == 0) {
            found = 1;
            printf("📦 Installing %s...\n", pkg_name);
            printf("📝 %s\n", packages[i].description);
            
            // Construire la commande
            char cmd[150];
            if (geteuid() != 0) {
                snprintf(cmd, sizeof(cmd), "sudo %s", packages[i].install_cmd);
            } else {
                snprintf(cmd, sizeof(cmd), "%s", packages[i].install_cmd);
            }
            
            printf("⚙️  %s\n", cmd);
            int result = run_command(cmd);
            
            if (result == 0) {
                printf("✅ %s installed successfully!\n", pkg_name);
            } else {
                printf("❌ Failed to install %s\n", pkg_name);
            }
            break;
        }
    }
    
    if (!found) {
        printf("❌ Package '%s' not found\n", pkg_name);
        printf("💡 Available packages:\n");
        for (int i = 0; i < package_count; i++) {
            printf("  %-10s", packages[i].name);
            if ((i + 1) % 4 == 0) printf("\n");
        }
        printf("\n");
    }
}

// Fonction pour lister les paquets
void list_packages() {
    printf("📦 Available packages (%d):\n", package_count);
    printf("========================================\n");
    
    for (int i = 0; i < package_count; i++) {
        printf("  • %-12s - %s\n", 
               packages[i].name, 
               packages[i].description);
    }
    
    printf("\n💡 Install: zarch install <package>\n");
}

// Fonction pour rechercher un paquet
void search_package(const char *keyword) {
    printf("🔍 Searching for '%s':\n", keyword);
    int found = 0;
    
    for (int i = 0; i < package_count; i++) {
        if (strstr(packages[i].name, keyword) != NULL || 
            strstr(packages[i].description, keyword) != NULL) {
            printf("  • %-12s - %s\n", 
                   packages[i].name, 
                   packages[i].description);
            found = 1;
        }
    }
    
    if (!found) {
        printf("❌ No packages found\n");
    }
}

// Fonction d'aide
void show_help() {
    printf("🐧 zarch - iSH Package Manager (C version)\n");
    printf("========================================\n");
    printf("Commands:\n");
    printf("  install <pkg...>  Install packages\n");
    printf("  list              List available packages\n");
    printf("  search <term>     Search packages\n");
    printf("  help              Show this help\n");
    printf("  version           Show version\n");
    printf("\nExamples:\n");
    printf("  zarch install git python3\n");
    printf("  zarch search editor\n");
    printf("  zarch list\n");
}

// Fonction version
void show_version() {
    printf("zarch v1.0.0 (Compiled C version)\n");
    printf("For iSH Alpine Linux\n");
}

int main(int argc, char *argv[]) {
    // Afficher le header
    printf("🐧 zarch - iSH Package Manager\n");
    printf("===============================\n");
    
    if (argc < 2) {
        list_packages();
        return 0;
    }
    
    // Traiter les commandes
    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            printf("❌ Usage: zarch install <package1> <package2> ...\n");
            return 1;
        }
        
        printf("🔧 Installation started...\n");
        printf("===============================\n");
        
        for (int i = 2; i < argc; i++) {
            install_package(argv[i]);
            if (i < argc - 1) printf("\n");
        }
        
        printf("\n===============================\n");
        printf("✅ Installation completed!\n");
        
    } else if (strcmp(argv[1], "list") == 0) {
        list_packages();
        
    } else if (strcmp(argv[1], "search") == 0) {
        if (argc < 3) {
            printf("❌ Usage: zarch search <keyword>\n");
            return 1;
        }
        search_package(argv[2]);
        
    } else if (strcmp(argv[1], "help") == 0) {
        show_help();
        
    } else if (strcmp(argv[1], "version") == 0) {
        show_version();
        
    } else {
        printf("❌ Unknown command: %s\n", argv[1]);
        printf("💡 Use: zarch help\n");
        return 1;
    }
    
    return 0;
}
