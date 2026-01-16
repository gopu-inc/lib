#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#define VERSION "5.3.0"
#define REGISTRY_URL "https://zenv-hub.onrender.com"
#define CONFIG_DIR ".zarch"
#define CONFIG_FILE "config.json"
#define CACHE_FILE "cache.json"
#define LIB_PATH "/usr/local/bin/swiftvelox/addws"  // Chez corrigé

#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"

struct MemoryStruct {
    char *memory;
    size_t size;
};

// Structure pour la configuration
typedef struct {
    char token[256];
    char username[64];
    char email[128];
    time_t last_update;
    char personal_code[16];
} Config;

// Structure pour les arguments
typedef struct {
    char command[32];
    char package_name[128];
    char username[64];
    char password[64];
    char personal_code[16];
    char scope[64];
    char path[256];
    int force;
    int verbose;
    int no_cache;
} Args;

// ============================================================================
// FONCTIONS UTILITAIRES
// ============================================================================

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    
    if (!ptr) {
        fprintf(stderr, RED "❌ Erreur d'allocation mémoire\n" RESET);
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

static size_t WriteFileCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    FILE *file = (FILE *)userp;
    size_t written = fwrite(contents, size, nmemb, file);
    return written;
}

void print_step(const char* icon, const char* msg) {
    printf("%s %s%s%s\n", icon, BLUE, msg, RESET);
}

void print_success(const char* msg) {
    printf(GREEN "✅ %s\n" RESET, msg);
}

void print_error(const char* msg) {
    fprintf(stderr, RED "❌ %s\n" RESET, msg);
}

void print_warning(const char* msg) {
    printf(YELLOW "⚠️ %s\n" RESET, msg);
}

void print_info(const char* msg) {
    printf(CYAN "ℹ️ %s\n" RESET, msg);
}

char* get_config_path(const char* filename) {
    static char path[512];
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(path, sizeof(path), "%s/%s/%s", home, CONFIG_DIR, filename);
    return path;
}

int ensure_config_dir() {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", getenv("HOME"), CONFIG_DIR);
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

int file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

// ============================================================================
// GESTION CONFIGURATION
// ============================================================================

int load_config(Config* config) {
    char* config_path = get_config_path(CONFIG_FILE);
    
    if (!file_exists(config_path)) {
        return 0;
    }
    
    FILE* f = fopen(config_path, "r");
    if (!f) {
        return 0;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buffer = malloc(size + 1);
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    json_t* root = json_loads(buffer, 0, NULL);
    free(buffer);
    
    if (!root) {
        return 0;
    }
    
    // Charger les valeurs
    const char* token = json_string_value(json_object_get(root, "token"));
    const char* username = json_string_value(json_object_get(root, "username"));
    const char* email = json_string_value(json_object_get(root, "email"));
    const char* personal_code = json_string_value(json_object_get(root, "personal_code"));
    
    if (token) strncpy(config->token, token, sizeof(config->token) - 1);
    if (username) strncpy(config->username, username, sizeof(config->username) - 1);
    if (email) strncpy(config->email, email, sizeof(config->email) - 1);
    if (personal_code) strncpy(config->personal_code, personal_code, sizeof(config->personal_code) - 1);
    
    config->last_update = json_integer_value(json_object_get(root, "last_update"));
    
    json_decref(root);
    return 1;
}

int save_config(const Config* config) {
    ensure_config_dir();
    char* config_path = get_config_path(CONFIG_FILE);
    
    json_t* root = json_object();
    json_object_set_new(root, "token", json_string(config->token));
    json_object_set_new(root, "username", json_string(config->username));
    json_object_set_new(root, "email", json_string(config->email));
    json_object_set_new(root, "personal_code", json_string(config->personal_code));
    json_object_set_new(root, "last_update", json_integer(time(NULL)));
    
    char* json_str = json_dumps(root, JSON_INDENT(2));
    FILE* f = fopen(config_path, "w");
    if (!f) {
        json_decref(root);
        free(json_str);
        return 0;
    }
    
    fprintf(f, "%s", json_str);
    fclose(f);
    
    json_decref(root);
    free(json_str);
    return 1;
}

// ============================================================================
// COMMANDES CLI
// ============================================================================

void show_help() {
    printf(BOLD "\n🐧 Zarch Package Manager v%s\n\n" RESET, VERSION);
    printf("Usage: zarch <command> [options]\n\n");
    printf("Commands:\n");
    printf("  login <username> <password>    Connexion au registre\n");
    printf("  logout                         Déconnexion\n");
    printf("  whoami                         Affiche l'utilisateur connecté\n");
    printf("  init                           Initialise un nouveau paquet\n");
    printf("  build [path]                   Construit un paquet\n");
    printf("  publish [path] [code]          Publie un paquet (code de sécurité requis)\n");
    printf("  install <package>              Installe un paquet\n");
    printf("  uninstall <package>            Désinstalle un paquet\n");
    printf("  search [query]                 Recherche des paquets\n");
    printf("  info <package>                 Affiche les infos d'un paquet\n");
    printf("  list                           Liste les paquets installés\n");
    printf("  update                         Met à jour l'index du registre\n");
    printf("  version                        Affiche la version\n");
    printf("\nOptions:\n");
    printf("  --scope=<scope>                Spécifie le scope (user/org)\n");
    printf("  --force                        Force l'opération\n");
    printf("  --verbose                      Mode verbeux\n");
    printf("  --no-cache                     Ignore le cache\n");
    printf("\nExamples:\n");
    printf("  zarch login john secret123\n");
    printf("  zarch init\n");
    printf("  zarch publish . 8A3F\n");
    printf("  zarch install @gopu-inc/mathlib\n");
    printf("  zarch search database\n");
}

void show_version() {
    printf("Zarch CLI v%s\n", VERSION);
    printf("Registry: %s\n", REGISTRY_URL);
    printf("Library Path: %s\n", LIB_PATH);
}

// --- LOGIN ---
int login_user(const char* username, const char* password) {
    print_step("🔐", "Connexion au registre Zarch...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("Échec d'initialisation CURL");
        return 0;
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    char url[512];
    snprintf(url, sizeof(url), "%s/api/auth/login", REGISTRY_URL);
    
    char json_data[512];
    snprintf(json_data, sizeof(json_data), 
             "{\"username\":\"%s\",\"password\":\"%s\"}", 
             username, password);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (http_code == 200) {
            json_t *resp = json_loads(chunk.memory, 0, NULL);
            if (resp) {
                const char* token = json_string_value(json_object_get(resp, "token"));
                const char* personal_code = json_string_value(json_object_get(resp, "personal_code"));
                
                if (token) {
                    Config config = {0};
                    strncpy(config.token, token, sizeof(config.token) - 1);
                    strncpy(config.username, username, sizeof(config.username) - 1);
                    if (personal_code) {
                        strncpy(config.personal_code, personal_code, sizeof(config.personal_code) - 1);
                    }
                    
                    if (save_config(&config)) {
                        print_success("Connexion réussie !");
                        if (personal_code) {
                            printf(MAGENTA "🔒 Votre code de sécurité : %s\n" RESET, personal_code);
                            printf(YELLOW "⚠️  Gardez ce code précieusement pour publier des paquets\n" RESET);
                        }
                    } else {
                        print_error("Échec de sauvegarde de la configuration");
                    }
                    
                    json_decref(resp);
                } else {
                    print_error("Token manquant dans la réponse");
                }
            } else {
                print_error("Réponse JSON invalide");
            }
        } else {
            json_t *error = json_loads(chunk.memory, 0, NULL);
            if (error) {
                const char* err_msg = json_string_value(json_object_get(error, "error"));
                print_error(err_msg ? err_msg : "Échec de connexion");
                json_decref(error);
            } else {
                print_error("Échec de connexion (code HTTP)");
            }
        }
    } else {
        print_error(curl_easy_strerror(res));
    }
    
    curl_easy_cleanup(curl);
    free(chunk.memory);
    return res == CURLE_OK;
}

// --- LOGOUT ---
void logout_user() {
    char* config_path = get_config_path(CONFIG_FILE);
    
    if (remove(config_path) == 0) {
        print_success("Déconnexion réussie");
    } else {
        print_error("Aucun utilisateur connecté");
    }
}

// --- WHOAMI ---
void whoami() {
    Config config;
    if (load_config(&config)) {
        printf("👤 Utilisateur: %s%s%s\n", GREEN, config.username, RESET);
        printf("📧 Email: %s\n", config.email[0] ? config.email : "Non défini");
        printf("🔗 Connecté à: %s\n", REGISTRY_URL);
    } else {
        print_error("Non connecté");
    }
}

// --- INIT ---
int init_package(const char* path) {
    print_step("🔄", "Initialisation d'un nouveau paquet...");
    
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
    
    if (file_exists(manifest_path)) {
        print_warning("Le fichier zarch.json existe déjà");
        return 0;
    }
    
    // Demander les informations
    char name[128], version[32], description[256], author[128], license[32];
    
    printf("Nom du paquet: ");
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = 0;
    
    printf("Version (ex: 1.0.0): ");
    fgets(version, sizeof(version), stdin);
    version[strcspn(version, "\n")] = 0;
    
    printf("Description: ");
    fgets(description, sizeof(description), stdin);
    description[strcspn(description, "\n")] = 0;
    
    printf("Auteur: ");
    fgets(author, sizeof(author), stdin);
    author[strcspn(author, "\n")] = 0;
    
    printf("License (MIT/GPL/BSD): ");
    fgets(license, sizeof(license), stdin);
    license[strcspn(license, "\n")] = 0;
    
    // Créer le manifest
    json_t* root = json_object();
    json_object_set_new(root, "name", json_string(name));
    json_object_set_new(root, "version", json_string(version));
    json_object_set_new(root, "description", json_string(description));
    json_object_set_new(root, "author", json_string(author));
    json_object_set_new(root, "license", json_string(license));
    json_object_set_new(root, "scope", json_string("user")); // Par défaut
    
    // Dependencies
    json_t* deps = json_object();
    json_object_set_new(root, "dependencies", deps);
    
    char* json_str = json_dumps(root, JSON_INDENT(2));
    FILE* f = fopen(manifest_path, "w");
    if (!f) {
        json_decref(root);
        free(json_str);
        print_error("Impossible de créer le manifest");
        return 0;
    }
    
    fprintf(f, "%s", json_str);
    fclose(f);
    
    // Créer une structure de base
    mkdir("src", 0755);
    
    FILE* main_file = fopen("src/main.c", "w");
    if (main_file) {
        fprintf(main_file, "// Package: %s\n// Version: %s\n\n", name, version);
        fprintf(main_file, "#include <stdio.h>\n\n");
        fprintf(main_file, "int main() {\n");
        fprintf(main_file, "    printf(\"Hello from %s!\\n\");\n", name);
        fprintf(main_file, "    return 0;\n}\n");
        fclose(main_file);
    }
    
    FILE* readme = fopen("README.md", "w");
    if (readme) {
        fprintf(readme, "# %s\n\n", name);
        fprintf(readme, "%s\n\n", description);
        fprintf(readme, "## Installation\n\n```bash\nzarch install %s\n```\n", name);
        fclose(readme);
    }
    
    json_decref(root);
    free(json_str);
    
    print_success("Paquet initialisé avec succès !");
    printf("📁 Structure créée:\n");
    printf("   ├── zarch.json\n");
    printf("   ├── README.md\n");
    printf("   └── src/main.c\n");
    
    return 1;
}

// --- BUILD ---
int build_package(const char* path, char* archive_out) {
    print_step("📦", "Construction du paquet...");
    
    char manifest[512];
    snprintf(manifest, sizeof(manifest), "%s/zarch.json", path);
    
    if (!file_exists(manifest)) {
        print_error("zarch.json non trouvé");
        return 0;
    }
    
    json_t* root = json_load_file(manifest, 0, NULL);
    if (!root) {
        print_error("Impossible de lire le manifest");
        return 0;
    }
    
    const char* name = json_string_value(json_object_get(root, "name"));
    const char* version = json_string_value(json_object_get(root, "version"));
    const char* scope = json_string_value(json_object_get(root, "scope"));
    
    if (!name || !version) {
        print_error("Nom ou version manquant dans le manifest");
        json_decref(root);
        return 0;
    }
    
    // Vérifier le format du nom
    if (strchr(name, '/')) {
        print_error("Le nom du paquet ne doit pas contenir de '/'");
        json_decref(root);
        return 0;
    }
    
    // Créer le nom de l'archive
    snprintf(archive_out, 512, "/tmp/%s-%s-%s.tar.gz", 
             scope ? scope : "user", name, version);
    
    printf("  Nom: %s\n", name);
    printf("  Version: %s\n", version);
    printf("  Scope: %s\n", scope ? scope : "user");
    printf("  Archive: %s\n", archive_out);
    
    // Construire l'archive
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -czf \"%s\" -C \"%s\" . 2>/dev/null", archive_out, path);
    
    int result = system(cmd);
    if (result != 0) {
        print_error("Échec de la création de l'archive");
        json_decref(root);
        return 0;
    }
    
    // Vérifier la taille
    struct stat st;
    if (stat(archive_out, &st) == 0) {
        printf("  Taille: %.2f MB\n", (double)st.st_size / (1024 * 1024));
    }
    
    json_decref(root);
    print_success("Paquet construit avec succès");
    return 1;
}

// --- PUBLISH ---
int publish_package(const char* path, const char* personal_code) {
    print_step("🚀", "Publication du paquet...");
    
    // Charger la configuration
    Config config;
    if (!load_config(&config)) {
        print_error("Non connecté. Utilisez 'zarch login'");
        return 0;
    }
    
    if (!config.token[0]) {
        print_error("Token d'authentification manquant");
        return 0;
    }
    
    // Construire le paquet
    char archive_path[512];
    if (!build_package(path, archive_path)) {
        return 0;
    }
    
    // Lire le manifest pour les métadonnées
    char manifest[512];
    snprintf(manifest, sizeof(manifest), "%s/zarch.json", path);
    json_t* root = json_load_file(manifest, 0, NULL);
    if (!root) {
        print_error("Impossible de lire le manifest");
        return 0;
    }
    
    const char* name = json_string_value(json_object_get(root, "name"));
    const char* version = json_string_value(json_object_get(root, "version"));
    const char* scope = json_string_value(json_object_get(root, "scope"));
    const char* description = json_string_value(json_object_get(root, "description"));
    
    if (!scope) scope = "user";
    
    // Vérifier le code personnel
    if (!personal_code || strlen(personal_code) < 4) {
        print_error("Code de sécurité requis (4+ caractères)");
        json_decref(root);
        return 0;
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("Échec d'initialisation CURL");
        json_decref(root);
        return 0;
    }
    
    // Préparer l'URL
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/package/upload/%s/%s?token=%s", 
             REGISTRY_URL, scope, name, config.token);
    
    // Préparer le formulaire
    struct curl_httppost *form = NULL;
    struct curl_httppost *last = NULL;
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "file",
                 CURLFORM_FILE, archive_path,
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "version",
                 CURLFORM_COPYCONTENTS, version,
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "description",
                 CURLFORM_COPYCONTENTS, description ? description : "",
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "license",
                 CURLFORM_COPYCONTENTS, json_string_value(json_object_get(root, "license")),
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "personal_code",
                 CURLFORM_COPYCONTENTS, personal_code,
                 CURLFORM_END);
    
    // Ajouter le README s'il existe
    char readme_path[512];
    snprintf(readme_path, sizeof(readme_path), "%s/README.md", path);
    if (file_exists(readme_path)) {
        curl_formadd(&form, &last,
                     CURLFORM_COPYNAME, "readme",
                     CURLFORM_FILE, readme_path,
                     CURLFORM_END);
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    
    print_step("📤", "Upload vers le registre...");
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (http_code == 200) {
            json_t* resp = json_loads(chunk.memory, 0, NULL);
            if (resp) {
                const char* message = json_string_value(json_object_get(resp, "message"));
                print_success(message ? message : "Publication réussie !");
                
                // Afficher les détails
                json_t* details = json_object_get(resp, "details");
                if (details) {
                    printf("  📊 Détails:\n");
                    const char* encryption = json_string_value(json_object_get(details, "encryption"));
                    int size_original = json_integer_value(json_object_get(details, "size_original"));
                    int size_secured = json_integer_value(json_object_get(details, "size_secured"));
                    
                    printf("     Chiffrement: %s\n", encryption);
                    printf("     Taille originale: %.2f KB\n", size_original / 1024.0);
                    printf("     Taille sécurisée: %.2f KB\n", size_secured / 1024.0);
                    printf("     Compression: %.1f%%\n", 
                           (1 - (double)size_secured / size_original) * 100);
                }
                
                json_decref(resp);
            }
        } else {
            json_t* error = json_loads(chunk.memory, 0, NULL);
            if (error) {
                const char* err_msg = json_string_value(json_object_get(error, "error"));
                print_error(err_msg ? err_msg : "Échec de publication");
                json_decref(error);
            } else {
                print_error("Échec de publication (code HTTP)");
            }
        }
    } else {
        print_error(curl_easy_strerror(res));
    }
    
    // Nettoyage
    curl_easy_cleanup(curl);
    curl_formfree(form);
    free(chunk.memory);
    json_decref(root);
    
    // Supprimer l'archive temporaire
    remove(archive_path);
    
    return res == CURLE_OK;
}

// --- INSTALL ---
int install_package(const char* pkg_name) {
    print_step("📥", "Installation du paquet...");
    printf("  Paquet: %s\n", pkg_name);
    
    // Extraire le scope et le nom
    char scope[64] = "user";
    char name[128];
    
    if (pkg_name[0] == '@') {
        // Format: @scope/name
        char* slash = strchr(pkg_name, '/');
        if (slash) {
            strncpy(scope, pkg_name + 1, slash - pkg_name - 1);
            scope[slash - pkg_name - 1] = '\0';
            strncpy(name, slash + 1, sizeof(name) - 1);
        } else {
            print_error("Format de paquet invalide");
            return 0;
        }
    } else {
        // Format simple: name
        strncpy(name, pkg_name, sizeof(name) - 1);
    }
    
    // Créer le chemin de destination
    char target[512];
    snprintf(target, sizeof(target), "%s/%s", LIB_PATH, name);
    
    // Vérifier si le paquet existe déjà
    if (file_exists(target) && !file_exists(target)) {
        print_warning("Le paquet semble déjà installé");
        printf("  Réinstaller ? [y/N]: ");
        char response[4];
        fgets(response, sizeof(response), stdin);
        if (response[0] != 'y' && response[0] != 'Y') {
            return 0;
        }
    }
    
    // Créer le dossier
    char cmd_dir[512];
    snprintf(cmd_dir, sizeof(cmd_dir), "mkdir -p \"%s\"", target);
    if (system(cmd_dir) != 0) {
        print_error("Impossible de créer le répertoire d'installation");
        return 0;
    }
    
    // Obtenir l'URL de téléchargement
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("Échec d'initialisation CURL");
        return 0;
    }
    
    // Récupérer l'index pour obtenir la version
    struct MemoryStruct chunk = {malloc(1), 0};
    char index_url[512];
    snprintf(index_url, sizeof(index_url), "%s/zarch/INDEX", REGISTRY_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, index_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        print_error("Impossible de récupérer l'index");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    json_t *index = json_loads(chunk.memory, 0, NULL);
    if (!index) {
        print_error("Index corrompu");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    // Chercher le paquet
    char full_name[256];
    if (strcmp(scope, "user") == 0) {
        snprintf(full_name, sizeof(full_name), "%s", name);
    } else {
        snprintf(full_name, sizeof(full_name), "@%s/%s", scope, name);
    }
    
    json_t *packages = json_object_get(index, "packages");
    json_t *pkg = json_object_get(packages, full_name);
    
    if (!pkg) {
        print_error("Paquet non trouvé dans le registre");
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    const char* version = json_string_value(json_object_get(pkg, "version"));
    printf("  Version: %s\n", version);
    
    // Télécharger le paquet
    char dl_url[1024];
    snprintf(dl_url, sizeof(dl_url), "%s/package/download/%s/%s/%s", 
             REGISTRY_URL, scope, name, version);
    
    char archive_path[512];
    snprintf(archive_path, sizeof(archive_path), "%s/%s-%s.tar.gz", target, name, version);
    
    print_step("⬇️", "Téléchargement...");
    
    FILE* archive_file = fopen(archive_path, "wb");
    if (!archive_file) {
        print_error("Impossible de créer le fichier d'archive");
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, dl_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, archive_file);
    
    res = curl_easy_perform(curl);
    fclose(archive_file);
    
    if (res != CURLE_OK) {
        print_error("Échec du téléchargement");
        remove(archive_path);
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    // Extraire l'archive
    print_step("📦", "Extraction...");
    
    char cmd_extract[1024];
    snprintf(cmd_extract, sizeof(cmd_extract), "tar -xzf \"%s\" -C \"%s\"", archive_path, target);
    
    if (system(cmd_extract) != 0) {
        print_error("Échec de l'extraction");
        remove(archive_path);
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    // Supprimer l'archive
    remove(archive_path);
    
    // Vérifier l'installation
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", target);
    
    if (file_exists(manifest_path)) {
        print_success("Installation terminée !");
        printf("  📍 Emplacement: %s\n", target);
    } else {
        print_warning("Installation terminée mais manifest non trouvé");
    }
    
    // Nettoyage
    json_decref(index);
    curl_easy_cleanup(curl);
    free(chunk.memory);
    
    return 1;
}

// --- UNINSTALL ---
int uninstall_package(const char* pkg_name) {
    print_step("🗑️", "Désinstallation...");
    
    // Chercher le paquet
    char target[512];
    snprintf(target, sizeof(target), "%s/%s", LIB_PATH, pkg_name);
    
    if (!file_exists(target)) {
        // Essayer avec différents formats
        char target2[512];
        snprintf(target2, sizeof(target2), "%s/@%s", LIB_PATH, pkg_name);
        
        if (file_exists(target2)) {
            strcpy(target, target2);
        } else {
            print_error("Paquet non trouvé");
            return 0;
        }
    }
    
    printf("  Paquet: %s\n", pkg_name);
    printf("  Emplacement: %s\n", target);
    printf("  Confirmer la suppression ? [y/N]: ");
    
    char response[4];
    fgets(response, sizeof(response), stdin);
    
    if (response[0] != 'y' && response[0] != 'Y') {
        print_info("Annulé");
        return 0;
    }
    
    // Supprimer récursivement
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", target);
    
    if (system(cmd) == 0) {
        print_success("Paquet désinstallé");
        return 1;
    } else {
        print_error("Échec de la désinstallation");
        return 0;
    }
}

// --- SEARCH ---
void search_registry(const char* query) {
    print_step("🔍", "Recherche dans le registre...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("Échec d'initialisation CURL");
        return;
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    char url[512];
    snprintf(url, sizeof(url), "%s/zarch/INDEX", REGISTRY_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        print_error("Impossible de récupérer l'index");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    json_t *index = json_loads(chunk.memory, 0, NULL);
    if (!index) {
        print_error("Index corrompu");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    json_t *packages = json_object_get(index, "packages");
    
    printf("\n");
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ %-40s │ %-10s │ %-10s │\n", "PAQUET", "VERSION", "SCOPE");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    
    const char* key;
    json_t *value;
    int found = 0;
    
    void *iter = json_object_iter(packages);
    while (iter) {
        key = json_object_iter_key(iter);
        value = json_object_iter_value(iter);
        
        // Filtrer par query si spécifié
        if (!query || strstr(key, query) || 
            strstr(json_string_value(json_object_get(value, "version")), query)) {
            
            printf("│ %-40s │ %-10s │ %-10s │\n",
                   key,
                   json_string_value(json_object_get(value, "version")),
                   json_string_value(json_object_get(value, "scope")));
            found++;
        }
        
        iter = json_object_iter_next(packages, iter);
    }
    
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("\n%s paquets trouvés\n", found ? GREEN : RED);
    printf(RESET);
    
    json_decref(index);
    curl_easy_cleanup(curl);
    free(chunk.memory);
}

// --- INFO ---
void package_info(const char* pkg_name) {
    print_step("📋", "Informations du paquet...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("Échec d'initialisation CURL");
        return;
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    char url[512];
    snprintf(url, sizeof(url), "%s/zarch/INDEX", REGISTRY_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        print_error("Impossible de récupérer l'index");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    json_t *index = json_loads(chunk.memory, 0, NULL);
    if (!index) {
        print_error("Index corrompu");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    json_t *packages = json_object_get(index, "packages");
    json_t *pkg = json_object_get(packages, pkg_name);
    
    if (!pkg) {
        print_error("Paquet non trouvé");
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    printf("\n");
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ %-50s │\n", pkg_name);
    printf("├─────────────────────────────────────────────────────────────┤\n");
    printf("│ %-20s: %-30s │\n", "Version", 
           json_string_value(json_object_get(pkg, "version")));
    printf("│ %-20s: %-30s │\n", "Scope", 
           json_string_value(json_object_get(pkg, "scope")));
    
    // Récupérer plus d'infos si disponible
    char info_url[512];
    snprintf(info_url, sizeof(info_url), "%s/package/%s/%s", 
             REGISTRY_URL, 
             json_string_value(json_object_get(pkg, "scope")),
             strchr(pkg_name, '/') ? strchr(pkg_name, '/') + 1 : pkg_name);
    
    curl_easy_setopt(curl, CURLOPT_URL, info_url);
    free(chunk.memory);
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl_easy_perform(curl);
    
    json_t *details = json_loads(chunk.memory, 0, NULL);
    if (details) {
        const char* description = json_string_value(json_object_get(details, "description"));
        const char* author = json_string_value(json_object_get(details, "author"));
        const char* license = json_string_value(json_object_get(details, "license"));
        
        if (description) printf("│ %-20s: %-30s │\n", "Description", description);
        if (author) printf("│ %-20s: %-30s │\n", "Auteur", author);
        if (license) printf("│ %-20s: %-30s │\n", "License", license);
        
        json_decref(details);
    }
    
    printf("└─────────────────────────────────────────────────────────────┘\n");
    
    json_decref(index);
    curl_easy_cleanup(curl);
    free(chunk.memory);
}

// --- LIST ---
void list_installed() {
    print_step("📁", "Paquets installés...");
    
    DIR *dir = opendir(LIB_PATH);
    if (!dir) {
        print_error("Répertoire d'installation non trouvé");
        return;
    }
    
    printf("\n");
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ %-40s │ %-20s │\n", "PAQUET", "EMPLACEMENT");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", LIB_PATH, entry->d_name);
            
            struct stat st;
            if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                printf("│ %-40s │ %-20s │\n", entry->d_name, full_path);
                count++;
            }
        }
    }
    
    closedir(dir);
    
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("\n%s paquets installés\n", count ? GREEN : RED);
    printf(RESET);
}

// --- UPDATE ---
void update_index() {
    print_step("🔄", "Mise à jour de l'index...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("Échec d'initialisation CURL");
        return;
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    char url[512];
    snprintf(url, sizeof(url), "%s/zarch/INDEX", REGISTRY_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        print_error("Échec de mise à jour");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    // Sauvegarder dans le cache
    ensure_config_dir();
    char* cache_path = get_config_path(CACHE_FILE);
    FILE* f = fopen(cache_path, "w");
    if (f) {
        fwrite(chunk.memory, 1, chunk.size, f);
        fclose(f);
        
        print_success("Index mis à jour");
    } else {
        print_error("Échec de sauvegarde du cache");
    }
    
    curl_easy_cleanup(curl);
    free(chunk.memory);
}

// ============================================================================
// PARSING DES ARGUMENTS
// ============================================================================

void parse_args(int argc, char** argv, Args* args) {
    memset(args, 0, sizeof(Args));
    strcpy(args->path, ".");
    
    if (argc < 2) {
        return;
    }
    
    strncpy(args->command, argv[1], sizeof(args->command) - 1);
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0) {
            args->verbose = 1;
        } else if (strcmp(argv[i], "--force") == 0) {
            args->force = 1;
        } else if (strcmp(argv[i], "--no-cache") == 0) {
            args->no_cache = 1;
        } else if (strncmp(argv[i], "--scope=", 8) == 0) {
            strncpy(args->scope, argv[i] + 8, sizeof(args->scope) - 1);
        } else if (strcmp(args->command, "login") == 0 && i == 2) {
            strncpy(args->username, argv[i], sizeof(args->username) - 1);
        } else if (strcmp(args->command, "login") == 0 && i == 3) {
            strncpy(args->password, argv[i], sizeof(args->password) - 1);
        } else if (strcmp(args->command, "publish") == 0 && i == 2) {
            strncpy(args->path, argv[i], sizeof(args->path) - 1);
        } else if (strcmp(args->command, "publish") == 0 && i == 3) {
            strncpy(args->personal_code, argv[i], sizeof(args->personal_code) - 1);
        } else if ((strcmp(args->command, "install") == 0 || 
                   strcmp(args->command, "uninstall") == 0 ||
                   strcmp(args->command, "info") == 0) && i == 2) {
            strncpy(args->package_name, argv[i], sizeof(args->package_name) - 1);
        } else if (strcmp(args->command, "search") == 0 && i == 2) {
            strncpy(args->package_name, argv[i], sizeof(args->package_name) - 1);
        } else if (strcmp(args->command, "build") == 0 && i == 2) {
            strncpy(args->path, argv[i], sizeof(args->path) - 1);
        }
    }
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char** argv) {
    // Vérifier CURL et les dépendances
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, RED "❌ Échec d'initialisation de CURL\n" RESET);
        return 1;
    }
    
    Args args;
    parse_args(argc, argv, &args);
    
    // Afficher le logo
    if (argc > 1) {
        printf(CYAN BOLD "\n🐧 Zarch Package Manager v%s\n" RESET, VERSION);
    }
    
    // Traiter les commandes
    if (argc < 2 || strcmp(args.command, "help") == 0) {
        show_help();
    } else if (strcmp(args.command, "version") == 0) {
        show_version();
    } else if (strcmp(args.command, "login") == 0) {
        if (argc < 4) {
            print_error("Usage: zarch login <username> <password>");
            return 1;
        }
        login_user(args.username, args.password);
    } else if (strcmp(args.command, "logout") == 0) {
        logout_user();
    } else if (strcmp(args.command, "whoami") == 0) {
        whoami();
    } else if (strcmp(args.command, "init") == 0) {
        init_package(".");
    } else if (strcmp(args.command, "build") == 0) {
        char archive[512];
        build_package(args.path, archive);
    } else if (strcmp(args.command, "publish") == 0) {
        if (argc < 4) {
            print_error("Usage: zarch publish [path] <personal_code>");
            return 1;
        }
        publish_package(args.path, args.personal_code);
    } else if (strcmp(args.command, "install") == 0) {
        if (argc < 3) {
            print_error("Usage: zarch install <package>");
            return 1;
        }
        install_package(args.package_name);
    } else if (strcmp(args.command, "uninstall") == 0) {
        if (argc < 3) {
            print_error("Usage: zarch uninstall <package>");
            return 1;
        }
        uninstall_package(args.package_name);
    } else if (strcmp(args.command, "search") == 0) {
        search_registry(argc >= 3 ? args.package_name : NULL);
    } else if (strcmp(args.command, "info") == 0) {
        if (argc < 3) {
            print_error("Usage: zarch info <package>");
            return 1;
        }
        package_info(args.package_name);
    } else if (strcmp(args.command, "list") == 0) {
        list_installed();
    } else if (strcmp(args.command, "update") == 0) {
        update_index();
    } else {
        print_error("Commande inconnue");
        show_help();
        return 1;
    }
    
    curl_global_cleanup();
    return 0;
}
