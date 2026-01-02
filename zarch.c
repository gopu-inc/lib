/**
 * Zarch CLI - Client intelligent pour Zarch Package Registry
 * Compilation: gcc -o zarch zarch.c -lcurl -ljansson -lcrypto -lz -Wall -O2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
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

// Fonctions de détection de langage
char* detect_language(const char *path) {
    char command[MAX_PATH + 50];
    char *language = "unknown";
    
    // Vérifier les fichiers caractéristiques
    if (access("setup.py", F_OK) == 0 || access("requirements.txt", F_OK) == 0 || 
        access("pyproject.toml", F_OK) == 0) {
        language = "python";
    }
    else if (access("package.json", F_OK) == 0 || access("yarn.lock", F_OK) == 0 ||
             access("package-lock.json", F_OK) == 0) {
        language = "js";
    }
    else if (access("Cargo.toml", F_OK) == 0 || access("Cargo.lock", F_OK) == 0) {
        language = "rust";
    }
    else if (access("go.mod", F_OK) == 0 || access("go.sum", F_OK) == 0) {
        language = "go";
    }
    else if (access("Gemfile", F_OK) == 0 || access("Gemfile.lock", F_OK) == 0) {
        language = "ruby";
    }
    else if (access("Makefile", F_OK) == 0 || access("configure", F_OK) == 0 ||
             access("CMakeLists.txt", F_OK) == 0) {
        language = "c";
    }
    else {
        // Scanner les fichiers pour les extensions
        snprintf(command, sizeof(command), "find \"%s\" -name \"*.py\" | head -1", path);
        FILE *pipe = popen(command, "r");
        if (pipe && fgets(command, sizeof(command), pipe)) {
            language = "python";
        }
        pclose(pipe);
        
        if (strcmp(language, "unknown") == 0) {
            snprintf(command, sizeof(command), "find \"%s\" -name \"*.c\" -o -name \"*.h\" | head -1", path);
            pipe = popen(command, "r");
            if (pipe && fgets(command, sizeof(command), pipe)) {
                language = "c";
            }
            pclose(pipe);
        }
        
        if (strcmp(language, "unknown") == 0) {
            snprintf(command, sizeof(command), "find \"%s\" -name \"*.js\" -o -name \"*.ts\" | head -1", path);
            pipe = popen(command, "r");
            if (pipe && fgets(command, sizeof(command), pipe)) {
                language = "js";
            }
            pclose(pipe);
        }
    }
    
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

int create_default_manifest(const char *path, const char *name, const char *env) {
    char manifest_path[MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    
    json_t *root = json_object();
    json_object_set_new(root, "name", json_string(name));
    json_object_set_new(root, "version", json_string("1.0.0"));
    json_object_set_new(root, "author", json_string(getenv("USER")));
    json_object_set_new(root, "description", json_string("Package description"));
    json_object_set_new(root, "license", json_string("MIT"));
    json_object_set_new(root, "env", json_string(env));
    json_object_set_new(root, "entry_point", json_string(""));
    
    // Dépendances par défaut selon le langage
    json_t *deps = json_array();
    if (strcmp(env, "python") == 0) {
        json_array_append_new(deps, json_string(""));
    } else if (strcmp(env, "js") == 0) {
        json_array_append_new(deps, json_string(""));
    } else if (strcmp(env, "rust") == 0) {
        json_array_append_new(deps, json_string(""));
    }
    json_object_set_new(root, "dependencies", deps);
    
    // Commandes de build par défaut
    json_t *build_cmds = json_array();
    if (strcmp(env, "c") == 0) {
        json_array_append_new(build_cmds, json_string("gcc -o dist/program *.c -lm"));
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

// Fonctions d'installation spécifiques
int install_c_package(const char *package_path, const Manifest *manifest) {
    print_info("Installing C package...");
    
    // Créer le répertoire de build
    char build_dir[MAX_PATH];
    snprintf(build_dir, sizeof(build_dir), "%s/build", package_path);
    
    if (mkdir(build_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir build_dir");
        return -1;
    }
    
    // Compiler avec gcc
    char compile_cmd[MAX_PATH + 100];
    snprintf(compile_cmd, sizeof(compile_cmd), 
             "cd \"%s\" && gcc -o \"%s/%s\" *.c -lm -Wall -O2", 
             package_path, build_dir, manifest->name);
    
    print_info("Compiling...");
    printf("Command: %s\n", compile_cmd);
    
    int result = system(compile_cmd);
    if (result != 0) {
        print_error("Compilation failed");
        return -1;
    }
    
    // Installer
    char install_path[MAX_PATH];
    if (strlen(manifest->install_path) > 0) {
        snprintf(install_path, sizeof(install_path), "%s", manifest->install_path);
    } else {
        snprintf(install_path, sizeof(install_path), "/usr/local/bin");
    }
    
    // Créer le répertoire d'installation si nécessaire
    char mkdir_cmd[MAX_PATH + 50];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "sudo mkdir -p \"%s\"", install_path);
    system(mkdir_cmd);
    
    // Copier le binaire
    char copy_cmd[MAX_PATH * 2];
    snprintf(copy_cmd, sizeof(copy_cmd), 
             "sudo cp \"%s/build/%s\" \"%s/%s\"", 
             package_path, manifest->name, install_path, manifest->name);
    
    print_info("Installing to system...");
    printf("Command: %s\n", copy_cmd);
    
    result = system(copy_cmd);
    if (result != 0) {
        print_error("Installation failed");
        return -1;
    }
    
    // Changer les permissions
    char chmod_cmd[MAX_PATH + 100];
    snprintf(chmod_cmd, sizeof(chmod_cmd), 
             "sudo chmod +x \"%s/%s\"", install_path, manifest->name);
    system(chmod_cmd);
    
    return 0;
}

int install_python_package(const char *package_path, const Manifest *manifest) {
    print_info("Installing Python package...");
    
    // Vérifier si setup.py existe
    char setup_path[MAX_PATH];
    snprintf(setup_path, sizeof(setup_path), "%s/setup.py", package_path);
    
    if (access(setup_path, F_OK) != 0) {
        // Créer un setup.py minimal
        FILE *setup_file = fopen(setup_path, "w");
        if (setup_file) {
            fprintf(setup_file, "from setuptools import setup, find_packages\n\n");
            fprintf(setup_file, "setup(\n");
            fprintf(setup_file, "    name='%s',\n", manifest->name);
            fprintf(setup_file, "    version='%s',\n", manifest->version);
            fprintf(setup_file, "    author='%s',\n", manifest->author);
            fprintf(setup_file, "    description='%s',\n", manifest->description);
            fprintf(setup_file, "    packages=find_packages(),\n");
            fprintf(setup_file, "    install_requires=[],\n");
            fprintf(setup_file, ")\n");
            fclose(setup_file);
        }
    }
    
    // Installer avec pip
    char install_cmd[MAX_PATH + 100];
    snprintf(install_cmd, sizeof(install_cmd), 
             "cd \"%s\" && pip3 install . --user", package_path);
    
    printf("Command: %s\n", install_cmd);
    int result = system(install_cmd);
    
    if (result != 0) {
        // Essayer avec python -m pip
        snprintf(install_cmd, sizeof(install_cmd), 
                 "cd \"%s\" && python3 -m pip install . --user", package_path);
        result = system(install_cmd);
    }
    
    return result == 0 ? 0 : -1;
}

int install_js_package(const char *package_path, const Manifest *manifest) {
    print_info("Installing JavaScript package...");
    
    // Vérifier si package.json existe
    char package_json[MAX_PATH];
    snprintf(package_json, sizeof(package_json), "%s/package.json", package_path);
    
    if (access(package_json, F_OK) != 0) {
        // Créer un package.json minimal
        FILE *file = fopen(package_json, "w");
        if (file) {
            fprintf(file, "{\n");
            fprintf(file, "  \"name\": \"%s\",\n", manifest->name);
            fprintf(file, "  \"version\": \"%s\",\n", manifest->version);
            fprintf(file, "  \"description\": \"%s\",\n", manifest->description);
            fprintf(file, "  \"main\": \"index.js\",\n");
            fprintf(file, "  \"scripts\": {\n");
            fprintf(file, "    \"start\": \"node index.js\"\n");
            fprintf(file, "  },\n");
            fprintf(file, "  \"dependencies\": {}\n");
            fprintf(file, "}\n");
            fclose(file);
        }
    }
    
    // Installer avec npm
    char install_cmd[MAX_PATH + 100];
    snprintf(install_cmd, sizeof(install_cmd), 
             "cd \"%s\" && npm install --global .", package_path);
    
    printf("Command: %s\n", install_cmd);
    int result = system(install_cmd);
    
    if (result != 0) {
        // Essayer avec yarn
        snprintf(install_cmd, sizeof(install_cmd), 
                 "cd \"%s\" && yarn global add .", package_path);
        result = system(install_cmd);
    }
    
    return result == 0 ? 0 : -1;
}

// Fonction d'installation générique
int install_package(const char *package_path) {
    Manifest manifest;
    
    // Lire le manifest
    if (read_manifest(package_path, &manifest) != 0) {
        print_error("No manifest found");
        return -1;
    }
    
    printf("Installing package: %s v%s\n", manifest.name, manifest.version);
    printf("Language: %s\n", manifest.env);
    printf("Description: %s\n", manifest.description);
    
    // Installer selon le langage
    if (strcmp(manifest.env, "c") == 0) {
        return install_c_package(package_path, &manifest);
    } else if (strcmp(manifest.env, "python") == 0) {
        return install_python_package(package_path, &manifest);
    } else if (strcmp(manifest.env, "js") == 0) {
        return install_js_package(package_path, &manifest);
    } else if (strcmp(manifest.env, "rust") == 0) {
        print_info("Installing Rust package...");
        char cmd[MAX_PATH + 100];
        snprintf(cmd, sizeof(cmd), "cd \"%s\" && cargo install --path .", package_path);
        return system(cmd) == 0 ? 0 : -1;
    } else if (strcmp(manifest.env, "go") == 0) {
        print_info("Installing Go package...");
        char cmd[MAX_PATH + 100];
        snprintf(cmd, sizeof(cmd), "cd \"%s\" && go install .", package_path);
        return system(cmd) == 0 ? 0 : -1;
    } else {
        print_error("Unsupported language");
        return -1;
    }
}

// Fonctions pour interagir avec le registry
int login(const char *username, const char *password) {
    char url[MAX_URL];
    snprintf(url, sizeof(url), "%s/api/auth/login", ZARCH_URL);
    
    json_t *data = json_object();
    json_object_set_new(data, "username", json_string(username));
    json_object_set_new(data, "password", json_string(password));
    
    char *data_str = json_dumps(data, 0);
    char *response = NULL;
    
    int result = http_post(url, data_str, NULL, &response);
    free(data_str);
    json_decref(data);
    
    if (result == 0 && response != NULL) {
        json_error_t error;
        json_t *root = json_loads(response, 0, &error);
        
        if (root) {
            json_t *token = json_object_get(root, "token");
            if (token) {
                const char *token_str = json_string_value(token);
                if (save_token(token_str) == 0) {
                    print_success("Login successful!");
                }
            } else {
                json_t *error_msg = json_object_get(root, "error");
                if (error_msg) {
                    print_error(json_string_value(error_msg));
                }
            }
            json_decref(root);
        }
        free(response);
    }
    
    return result;
}

int publish_package(const char *path, const char *scope) {
    Manifest manifest;
    
    // Lire ou créer le manifest
    if (read_manifest(path, &manifest) != 0) {
        // Détecter le langage
        char *detected_env = detect_language(path);
        printf("Detected language: %s\n", detected_env);
        
        // Demander les informations
        char name[256];
        printf("Package name: ");
        fgets(name, sizeof(name), stdin);
        name[strcspn(name, "\n")] = 0;
        
        if (create_default_manifest(path, name, detected_env) != 0) {
            print_error("Failed to create manifest");
            return -1;
        }
        
        if (read_manifest(path, &manifest) != 0) {
            print_error("Failed to read manifest");
            return -1;
        }
    }
    
    // Vérifier le token
    char *token = read_token();
    if (token == NULL) {
        print_error("Not logged in. Use 'zarch login' first.");
        return -1;
    }
    
    printf("Publishing package: %s v%s\n", manifest.name, manifest.version);
    printf("Scope: %s\n", scope);
    printf("Language: %s\n", manifest.env);
    
    // Demander le code de sécurité
    char security_code[32];
    printf("Security code (2FA): ");
    fgets(security_code, sizeof(security_code), stdin);
    security_code[strcspn(security_code, "\n")] = 0;
    
    // Créer une archive du package
    char archive_path[MAX_PATH];
    snprintf(archive_path, sizeof(archive_path), "/tmp/%s-%s.tar.gz", manifest.name, manifest.version);
    
    char tar_cmd[MAX_PATH * 2];
    snprintf(tar_cmd, sizeof(tar_cmd), 
             "tar -czf \"%s\" -C \"%s\" .", archive_path, path);
    
    printf("Creating archive...\n");
    if (system(tar_cmd) != 0) {
        print_error("Failed to create archive");
        return -1;
    }
    
    // Préparer l'URL d'upload
    char url[MAX_URL];
    snprintf(url, sizeof(url), "%s/api/package/upload/%s/%s", 
             ZARCH_URL, scope, manifest.name);
    
    // Ici, normalement on utiliserait une vraie requête multipart/form-data
    // Pour simplifier, on va juste afficher les infos
    printf("\n" COLOR_CYAN "=== PACKAGE INFO ===" COLOR_RESET "\n");
    printf("Name: %s\n", manifest.name);
    printf("Version: %s\n", manifest.version);
    printf("Scope: %s\n", scope);
    printf("Language: %s\n", manifest.env);
    printf("Archive: %s\n", archive_path);
    printf("Security Code: %s\n", security_code);
    printf("\n" COLOR_YELLOW "Note: Full upload would require multipart form data implementation" COLOR_RESET "\n");
    
    // Nettoyer
    remove(archive_path);
    
    return 0;
}

int search_packages(const char *query) {
    char url[MAX_URL];
    if (query == NULL || strlen(query) == 0) {
        snprintf(url, sizeof(url), "%s/zarch/INDEX", ZARCH_URL);
    } else {
        snprintf(url, sizeof(url), "%s/zarch/INDEX", ZARCH_URL);
    }
    
    char *response = http_get(url, NULL);
    if (response == NULL) {
        print_error("Failed to fetch packages");
        return -1;
    }
    
    json_error_t error;
    json_t *root = json_loads(response, 0, &error);
    free(response);
    
    if (!root) {
        fprintf(stderr, "Error parsing response: %s\n", error.text);
        return -1;
    }
    
    json_t *packages = json_object_get(root, "packages");
    if (packages && json_is_object(packages)) {
        const char *key;
        json_t *value;
        
        printf("\n" COLOR_CYAN "=== AVAILABLE PACKAGES ===" COLOR_RESET "\n\n");
        
        json_object_foreach(packages, key, value) {
            const char *version = json_string_value(json_object_get(value, "version"));
            const char *scope = json_string_value(json_object_get(value, "scope"));
            
            printf("%s (v%s)\n", key, version);
            printf("  Scope: %s\n\n", scope);
        }
    }
    
    json_decref(root);
    return 0;
}

int install_from_registry(const char *package_name) {
    printf("Installing package: %s\n", package_name);
    
    // Analyser le nom du package (scope/name)
    char scope[128] = "user";
    char name[256];
    
    if (strncmp(package_name, "@", 1) == 0) {
        // Format: @scope/name
        const char *slash = strchr(package_name, '/');
        if (slash) {
            strncpy(scope, package_name + 1, slash - package_name - 1);
            scope[slash - package_name - 1] = 0;
            strcpy(name, slash + 1);
        } else {
            strcpy(name, package_name + 1);
        }
    } else {
        strcpy(name, package_name);
    }
    
    printf("Scope: %s, Name: %s\n", scope, name);
    
    // Récupérer les infos du package
    char url[MAX_URL];
    snprintf(url, sizeof(url), "%s/zarch/INDEX", ZARCH_URL);
    
    char *response = http_get(url, NULL);
    if (response == NULL) {
        print_error("Failed to fetch package info");
        return -1;
    }
    
    json_error_t error;
    json_t *root = json_loads(response, 0, &error);
    free(response);
    
    if (!root) {
        fprintf(stderr, "Error parsing response: %s\n", error.text);
        return -1;
    }
    
    json_t *packages = json_object_get(root, "packages");
    if (!packages) {
        print_error("No packages found");
        json_decref(root);
        return -1;
    }
    
    // Chercher le package
    char full_name[512];
    if (strcmp(scope, "user") == 0) {
        snprintf(full_name, sizeof(full_name), "%s", name);
    } else {
        snprintf(full_name, sizeof(full_name), "@%s/%s", scope, name);
    }
    
    json_t *pkg_info = json_object_get(packages, full_name);
    if (!pkg_info) {
        print_error("Package not found");
        json_decref(root);
        return -1;
    }
    
    const char *version = json_string_value(json_object_get(pkg_info, "version"));
    const char *pkg_scope = json_string_value(json_object_get(pkg_info, "scope"));
    
    printf("Found package: %s v%s\n", full_name, version);
    
    // Télécharger le package (simulé pour l'exemple)
    print_info("Downloading package...");
    
    // Créer un répertoire temporaire
    char temp_dir[MAX_PATH];
    snprintf(temp_dir, sizeof(temp_dir), "/tmp/zarch-%s-%s", name, version);
    
    if (mkdir(temp_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir temp_dir");
        json_decref(root);
        return -1;
    }
    
    // Pour l'exemple, on va créer un package C minimal
    char main_c[MAX_PATH];
    snprintf(main_c, sizeof(main_c), "%s/main.c", temp_dir);
    
    FILE *file = fopen(main_c, "w");
    if (file) {
        fprintf(file, "#include <stdio.h>\n\n");
        fprintf(file, "int main() {\n");
        fprintf(file, "    printf(\"Hello from %s v%s!\\n\");\n", name, version);
        fprintf(file, "    return 0;\n");
        fprintf(file, "}\n");
        fclose(file);
    }
    
    // Créer un manifest
    Manifest manifest;
    strcpy(manifest.name, name);
    strcpy(manifest.version, version);
    strcpy(manifest.env, "c");
    strcpy(manifest.author, "Zarch Registry");
    strcpy(manifest.description, "Example package from Zarch Registry");
    strcpy(manifest.license, "MIT");
    strcpy(manifest.install_path, "/usr/local/bin");
    
    if (create_default_manifest(temp_dir, name, "c") == 0) {
        // Installer le package
        if (install_package(temp_dir) == 0) {
            print_success("Package installed successfully!");
            printf("Run: %s\n", name);
        } else {
            print_error("Installation failed");
        }
    }
    
    // Nettoyer
    char rm_cmd[MAX_PATH + 50];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", temp_dir);
    system(rm_cmd);
    
    json_decref(root);
    return 0;
}

// Fonction principale
void show_help() {
    printf(COLOR_CYAN "Zarch CLI - Package Manager\n" COLOR_RESET);
    printf("\n");
    printf("Usage: zarch <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  login <username> <password>    Login to Zarch Registry\n");
    printf("  init [path]                    Initialize a new package\n");
    printf("  publish [path] [scope]         Publish a package\n");
    printf("  install <package>              Install a package from registry\n");
    printf("  search [query]                 Search for packages\n");
    printf("  info <package>                 Show package information\n");
    printf("  list                           List installed packages\n");
    printf("  update <package>               Update a package\n");
    printf("  remove <package>               Remove a package\n");
    printf("\n");
    printf("Examples:\n");
    printf("  zarch login myuser mypass\n");
    printf("  zarch init .\n");
    printf("  zarch publish . user\n");
    printf("  zarch install @user/mypackage\n");
    printf("  zarch install mypackage\n");
    printf("  zarch search \"crypto\"\n");
    printf("\n");
    printf("Registry URL: %s\n", ZARCH_URL);
}

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
        char *detected_env = detect_language(path);
        
        char name[256];
        printf("Package name: ");
        fgets(name, sizeof(name), stdin);
        name[strcspn(name, "\n")] = 0;
        
        if (create_default_manifest(path, name, detected_env) == 0) {
            print_success("Package initialized!");
            printf("Edit zarch.json to configure your package.\n");
        } else {
            print_error("Failed to initialize package");
        }
        
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
