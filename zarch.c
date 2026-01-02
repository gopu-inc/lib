/**
 * Zarch CLI - Client intelligent pour Zarch Package Registry
 * Version corrigée avec gestion d'entrée améliorée
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>  // Ajouté pour errno
#include <curl/curl.h>
#include <jansson.h>
#include <openssl/sha.h>
#include <zlib.h>

// Configuration
#define ZARCH_URL "https://zenv-hub.onrender.com"
#define CONFIG_DIR ".zarch"
#define CONFIG_FILE "config.json"
#define TOKEN_FILE "token"
#define CACHE_DIR "cache"
#define MAX_PATH 4096
#define MAX_URL 2048
#define MAX_BUF 8192

// Couleurs terminal
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"

// Structure pour la réponse HTTP
struct MemoryStruct {
    char *memory;
    size_t size;
};

// Structure pour le manifest
typedef struct {
    char name[256];
    char version[64];
    char author[256];
    char description[1024];
    char license[64];
    char env[32]; // "c", "python", "rust", "go", "js", "ruby"
    char entry_point[256];
    char dependencies[2048];
    char build_commands[2048];
    char install_path[256];
    char created_at[64];
    char updated_at[64];
} Manifest;

// Structure pour le package
typedef struct {
    char scope[128];
    char name[256];
    char version[64];
    char description[1024];
    char env[32];
    char author[256];
    int downloads;
    char created_at[64];
    char updated_at[64];
} PackageInfo;

// Fonctions utilitaires
void print_error(const char *message) {
    fprintf(stderr, COLOR_RED "[ERROR] %s" COLOR_RESET "\n", message);
}

void print_success(const char *message) {
    printf(COLOR_GREEN "[SUCCESS] %s" COLOR_RESET "\n", message);
}

void print_info(const char *message) {
    printf(COLOR_BLUE "[INFO] %s" COLOR_RESET "\n", message);
}

void print_warning(const char *message) {
    printf(COLOR_YELLOW "[WARNING] %s" COLOR_RESET "\n", message);
}

// Callback pour CURL
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) {
        fprintf(stderr, "Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

// Fonctions de configuration
char* get_config_dir() {
    static char path[MAX_PATH];
    char *home = getenv("HOME");
    if (home == NULL) {
        home = ".";
    }
    snprintf(path, sizeof(path), "%s/%s", home, CONFIG_DIR);
    return path;
}

char* get_config_file_path() {
    static char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", get_config_dir(), CONFIG_FILE);
    return path;
}

char* get_token_file_path() {
    static char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", get_config_dir(), TOKEN_FILE);
    return path;
}

char* get_cache_dir() {
    static char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", get_config_dir(), CACHE_DIR);
    return path;
}

int create_directories() {
    char *config_dir = get_config_dir();
    char *cache_dir = get_cache_dir();
    
    if (mkdir(config_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir config_dir");
        return -1;
    }
    
    if (mkdir(cache_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir cache_dir");
        return -1;
    }
    
    return 0;
}

// Fonctions HTTP
char* http_get(const char *url, const char *token) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        char auth_header[256];
        
        if (token != NULL && strlen(token) > 0) {
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
            headers = curl_slist_append(headers, auth_header);
        }
        
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "User-Agent: Zarch-CLI/1.0");
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            chunk.memory = NULL;
        }
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    
    return chunk.memory;
}

int http_post(const char *url, const char *data, const char *token, char **response) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        char auth_header[256];
        
        if (token != NULL && strlen(token) > 0) {
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
            headers = curl_slist_append(headers, auth_header);
        }
        
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "User-Agent: Zarch-CLI/1.0");
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            return -1;
        }
        
        *response = chunk.memory;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    
    return 0;
}

// Fonctions de token
char* read_token() {
    FILE *file = fopen(get_token_file_path(), "r");
    if (file == NULL) {
        return NULL;
    }
    
    static char token[512];
    if (fgets(token, sizeof(token), file) == NULL) {
        fclose(file);
        return NULL;
    }
    
    fclose(file);
    
    // Supprimer le saut de ligne
    token[strcspn(token, "\n")] = 0;
    return token;
}

int save_token(const char *token) {
    FILE *file = fopen(get_token_file_path(), "w");
    if (file == NULL) {
        return -1;
    }
    
    fprintf(file, "%s\n", token);
    fclose(file);
    return 0;
}

// Fonction améliorée pour détecter le langage
char* detect_language(const char *path) {
    static char language[32] = "unknown";
    char current_dir[MAX_PATH];
    
    // Obtenir le répertoire courant
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        strcpy(language, "c");
        return language;
    }
    
    // Vérifier si on peut accéder au chemin
    if (access(path, F_OK) != 0) {
        // Si le chemin n'existe pas, utiliser le répertoire courant
        strcpy(language, "c"); // Par défaut C
        return language;
    }
    
    // Changer vers le répertoire cible temporairement
    char original_dir[MAX_PATH];
    getcwd(original_dir, sizeof(original_dir));
    
    if (chdir(path) != 0) {
        // Ne pas changer de répertoire si échec
        strcpy(language, "c");
        return language;
    }
    
    // Vérifier les fichiers caractéristiques (version corrigée)
    if (access("setup.py", F_OK) == 0 || access("requirements.txt", F_OK) == 0 || 
        access("pyproject.toml", F_OK) == 0) {
        strcpy(language, "python");
    }
    else if (access("package.json", F_OK) == 0 || access("yarn.lock", F_OK) == 0 ||
             access("package-lock.json", F_OK) == 0) {
        strcpy(language, "js");
    }
    else if (access("Cargo.toml", F_OK) == 0 || access("Cargo.lock", F_OK) == 0) {
        strcpy(language, "rust");
    }
    else if (access("go.mod", F_OK) == 0 || access("go.sum", F_OK) == 0) {
        strcpy(language, "go");
    }
    else if (access("Gemfile", F_OK) == 0 || access("Gemfile.lock", F_OK) == 0) {
        strcpy(language, "ruby");
    }
    else if (access("Makefile", F_OK) == 0 || access("configure", F_OK) == 0 ||
             access("CMakeLists.txt", F_OK) == 0) {
        strcpy(language, "c");
    }
    else {
        // Par défaut, utiliser C
        strcpy(language, "c");
    }
    
    // Retourner au répertoire original
    chdir(original_dir);
    
    return language;
}

// Fonctions de manifest
int read_manifest(const char *path, Manifest *manifest) {
    char manifest_path[MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
    
    FILE *file = fopen(manifest_path, "r");
    if (file == NULL) {
        return -1;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *buffer = malloc(size + 1);
    fread(buffer, 1, size, file);
    buffer[size] = 0;
    fclose(file);
    
    json_error_t error;
    json_t *root = json_loads(buffer, 0, &error);
    free(buffer);
    
    if (!root) {
        fprintf(stderr, "Error parsing manifest: %s\n", error.text);
        return -1;
    }
    
    // Lire les champs du manifest
    json_t *name = json_object_get(root, "name");
    json_t *version = json_object_get(root, "version");
    json_t *author = json_object_get(root, "author");
    json_t *description = json_object_get(root, "description");
    json_t *license = json_object_get(root, "license");
    json_t *env = json_object_get(root, "env");
    json_t *entry_point = json_object_get(root, "entry_point");
    json_t *dependencies = json_object_get(root, "dependencies");
    json_t *build_commands = json_object_get(root, "build_commands");
    json_t *install_path = json_object_get(root, "install_path");
    json_t *created_at = json_object_get(root, "created_at");
    json_t *updated_at = json_object_get(root, "updated_at");
    
    if (name) strncpy(manifest->name, json_string_value(name), sizeof(manifest->name));
    if (version) strncpy(manifest->version, json_string_value(version), sizeof(manifest->version));
    if (author) strncpy(manifest->author, json_string_value(author), sizeof(manifest->author));
    if (description) strncpy(manifest->description, json_string_value(description), sizeof(manifest->description));
    if (license) strncpy(manifest->license, json_string_value(license), sizeof(manifest->license));
    if (env) strncpy(manifest->env, json_string_value(env), sizeof(manifest->env));
    if (entry_point) strncpy(manifest->entry_point, json_string_value(entry_point), sizeof(manifest->entry_point));
    
    if (dependencies) {
        char *deps_str = json_dumps(dependencies, JSON_COMPACT);
        strncpy(manifest->dependencies, deps_str, sizeof(manifest->dependencies));
        free(deps_str);
    }
    
    if (build_commands) {
        char *build_str = json_dumps(build_commands, JSON_COMPACT);
        strncpy(manifest->build_commands, build_str, sizeof(manifest->build_commands));
        free(build_str);
    }
    
    if (install_path) strncpy(manifest->install_path, json_string_value(install_path), sizeof(manifest->install_path));
    if (created_at) strncpy(manifest->created_at, json_string_value(created_at), sizeof(manifest->created_at));
    if (updated_at) strncpy(manifest->updated_at, json_string_value(updated_at), sizeof(manifest->updated_at));
    
    json_decref(root);
    return 0;
}

// Fonction améliorée pour créer un manifest
int create_default_manifest(const char *path, const char *name, const char *env) {
    char manifest_path[MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
    
    // Vérifier si le fichier existe déjà
    if (access(manifest_path, F_OK) == 0) {
        print_warning("Manifest already exists");
        return 0;
    }
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    
    json_t *root = json_object();
    json_object_set_new(root, "name", json_string(name));
    json_object_set_new(root, "version", json_string("1.0.0"));
    
    char *author = getenv("USER");
    if (author == NULL) author = "unknown";
    json_object_set_new(root, "author", json_string(author));
    
    json_object_set_new(root, "description", json_string("A Zarch package"));
    json_object_set_new(root, "license", json_string("MIT"));
    json_object_set_new(root, "env", json_string(env));
    json_object_set_new(root, "entry_point", json_string(""));
    
    // Dépendances vides
    json_t *deps = json_array();
    json_object_set_new(root, "dependencies", deps);
    
    // Commandes de build par défaut
    json_t *build_cmds = json_array();
    if (strcmp(env, "c") == 0) {
        json_array_append_new(build_cmds, json_string("gcc -o program *.c -lm"));
    } else if (strcmp(env, "python") == 0) {
        json_array_append_new(build_cmds, json_string("python setup.py build"));
    }
    json_object_set_new(root, "build_commands", build_cmds);
    
    json_object_set_new(root, "install_path", json_string("/usr/local/bin"));
    json_object_set_new(root, "created_at", json_string(timestamp));
    json_object_set_new(root, "updated_at", json_string(timestamp));
    
    char *manifest_str = json_dumps(root, JSON_INDENT(2));
    
    FILE *file = fopen(manifest_path, "w");
    if (file == NULL) {
        print_error("Cannot create manifest file");
        free(manifest_str);
        json_decref(root);
        return -1;
    }
    
    fprintf(file, "%s\n", manifest_str);
    fclose(file);
    
    free(manifest_str);
    json_decref(root);
    return 0;
}

// Fonction améliorée pour initialiser un package
int init_package(const char *path) {
    char abs_path[MAX_PATH];
    
    // Obtenir le chemin absolu
    if (realpath(path, abs_path) == NULL) {
        // Si le chemin n'existe pas, utiliser le chemin fourni
        strncpy(abs_path, path, sizeof(abs_path));
    }
    
    print_info("Initializing package...");
    printf("Path: %s\n", abs_path);
    
    // Détecter le langage
    char *detected_env = detect_language(abs_path);
    printf("Detected language: %s\n", detected_env);
    
    // Demander le nom du package
    char name[256];
    printf("Package name: ");
    fflush(stdout);  // Important pour afficher la question
    
    if (fgets(name, sizeof(name), stdin) == NULL) {
        print_error("Failed to read package name");
        return -1;
    }
    
    // Supprimer le saut de ligne
    name[strcspn(name, "\n")] = 0;
    
    // Si aucun nom n'est fourni, utiliser le nom du dossier
    if (strlen(name) == 0) {
        // Extraire le nom du dossier du chemin
        char *last_slash = strrchr(abs_path, '/');
        if (last_slash != NULL && strlen(last_slash) > 1) {
            strncpy(name, last_slash + 1, sizeof(name));
        } else {
            strncpy(name, "my-package", sizeof(name));
        }
        printf("Using default name: %s\n", name);
    }
    
    // Créer le manifest
    if (create_default_manifest(abs_path, name, detected_env) == 0) {
        print_success("Package initialized!");
        printf("Manifest created: %s/zarch.json\n", abs_path);
        printf("\nEdit zarch.json to configure your package.\n");
        return 0;
    } else {
        print_error("Failed to initialize package");
        return -1;
    }
}

// ... [le reste des fonctions reste inchangé jusqu'à la fonction principale] ...

// Fonction principale corrigée
int main(int argc, char *argv[]) {
    if (argc < 2) {
        show_help();
        return 1;
    }
    
    // Initialiser CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Créer les répertoires de configuration
    create_directories();
    
    const char *command = argv[1];
    
    if (strcmp(command, "login") == 0) {
        if (argc < 4) {
            print_error("Usage: zarch login <username> <password>");
            return 1;
        }
        return login(argv[2], argv[3]) == 0 ? 0 : 1;
        
    } else if (strcmp(command, "init") == 0) {
        const char *path = argc > 2 ? argv[2] : ".";
        return init_package(path) == 0 ? 0 : 1;
        
    } else if (strcmp(command, "publish") == 0) {
        const char *path = argc > 2 ? argv[2] : ".";
        const char *scope = argc > 3 ? argv[3] : "user";
        return publish_package(path, scope) == 0 ? 0 : 1;
        
    } else if (strcmp(command, "install") == 0) {
        if (argc < 3) {
            print_error("Usage: zarch install <package>");
            return 1;
        }
        return install_from_registry(argv[2]) == 0 ? 0 : 1;
        
    } else if (strcmp(command, "search") == 0) {
        const char *query = argc > 2 ? argv[2] : "";
        return search_packages(query) == 0 ? 0 : 1;
        
    } else if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0) {
        show_help();
        
    } else {
        print_error("Unknown command");
        show_help();
        return 1;
    }
    
    curl_global_cleanup();
    return 0;
}
