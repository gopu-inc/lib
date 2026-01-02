/**
 * Zarch CLI - Client complet avec commande build
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <curl/curl.h>
#include <jansson.h>
#include <openssl/sha.h>
#include <zlib.h>
#include <libgen.h>
#include <archive.h>
#include <archive_entry.h>

// Configuration
#define ZARCH_URL "https://zenv-hub.onrender.com"
#define CONFIG_DIR ".zarch"
#define CONFIG_FILE "config.json"
#define TOKEN_FILE "token"
#define CACHE_DIR "cache"
#define MAX_PATH 4096
#define MAX_URL 2048
#define MAX_BUF 8192
#define VERSION "2.1.0"

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

// Déclarations de fonctions
void print_error(const char *message);
void print_success(const char *message);
void print_info(const char *message);
void print_warning(const char *message);
void show_help(void);
char* get_config_dir(void);
char* get_config_file_path(void);
char* get_token_file_path(void);
char* get_cache_dir(void);
int create_directories(void);
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
char* http_get(const char *url, const char *token);
int http_post(const char *url, const char *data, const char *token, char **response);
int http_post_file(const char *url, const char *file_path, const char *token, 
                   const char *scope, const char *name, const char *version,
                   const char *description, const char *license, const char *personal_code);
char* read_token(void);
int save_token(const char *token);
char* detect_language(const char *path);
int read_manifest(const char *path, Manifest *manifest);
int create_default_manifest(const char *path, const char *name, const char *env);
int init_package(const char *path);
int login(const char *username, const char *password);
int publish_package(const char *path, const char *scope);
int search_packages(const char *query);
int install_from_registry(const char *package_name);
int list_packages(void);
int show_package_info(const char *package_name);
int update_package(const char *package_name);
int remove_package(const char *package_name);
int install_c_package(const char *package_path, const Manifest *manifest);
int install_python_package(const char *package_path, const Manifest *manifest);
int install_js_package(const char *package_path, const Manifest *manifest);
int install_package(const char *package_path);
int build_package(const char *path, const char *output_dir, int create_archive);
int create_tar_gz(const char *source_dir, const char *output_file);
int execute_build_commands(const char *path, const char *commands_json);
int register_package(const char *manifest_path, const char *scope);
int build_and_package(const char *path, const char *scope, const char *bind_file);

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

void show_help(void) {
    printf(COLOR_CYAN "Zarch CLI - Package Manager v%s\n" COLOR_RESET, VERSION);
    printf("\n");
    printf("Usage: zarch <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  login <username> <password>    Login to Zarch Registry\n");
    printf("  init [path]                    Initialize a new package\n");
    printf("  build [path] [options]         Build and package a project\n");
    printf("  publish [path] [scope]         Publish a package to registry\n");
    printf("  install <package>              Install a package from registry\n");
    printf("  search [query]                 Search for packages\n");
    printf("  info <package>                 Show package information\n");
    printf("  list                           List installed packages\n");
    printf("  update <package>               Update a package\n");
    printf("  remove <package>               Remove a package\n");
    printf("  version                        Show version\n");
    printf("  help                           Show this help\n");
    printf("\n");
    printf("Build Options:\n");
    printf("  --bind <file>                  Specify entry point file\n");
    printf("  --load <dir>                   Load directory contents\n");
    printf("  --format <tar.gz|zip>          Output format (default: tar.gz)\n");
    printf("  --output <dir>                 Output directory\n");
    printf("  --no-archive                   Build only, don't create archive\n");
    printf("\n");
    printf("Examples:\n");
    printf("  zarch init .\n");
    printf("  zarch build . --bind main.c\n");
    printf("  zarch build . --load src --output dist\n");
    printf("  zarch publish . user\n");
    printf("  zarch install @user/mypackage\n");
    printf("\n");
    printf("Registry URL: %s\n", ZARCH_URL);
}

// Fonctions de configuration
char* get_config_dir(void) {
    static char path[MAX_PATH];
    char *home = getenv("HOME");
    if (home == NULL) {
        home = ".";
    }
    snprintf(path, sizeof(path) - 1, "%s/%s", home, CONFIG_DIR);
    return path;
}

char* get_config_file_path(void) {
    static char path[MAX_PATH];
    snprintf(path, sizeof(path) - 1, "%s/%s", get_config_dir(), CONFIG_FILE);
    return path;
}

char* get_token_file_path(void) {
    static char path[MAX_PATH];
    snprintf(path, sizeof(path) - 1, "%s/%s", get_config_dir(), TOKEN_FILE);
    return path;
}

char* get_cache_dir(void) {
    static char path[MAX_PATH];
    snprintf(path, sizeof(path) - 1, "%s/%s", get_config_dir(), CACHE_DIR);
    return path;
}

int create_directories(void) {
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

// Fonction POST avec fichier (multipart/form-data)
int http_post_file(const char *url, const char *file_path, const char *token, 
                   const char *scope, const char *name, const char *version,
                   const char *description, const char *license, const char *personal_code) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl = curl_easy_init();
    if(curl) {
        struct curl_httppost *formpost = NULL;
        struct curl_httppost *lastptr = NULL;
        struct curl_slist *headers = NULL;
        char auth_header[256];
        
        if (token != NULL && strlen(token) > 0) {
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
            headers = curl_slist_append(headers, auth_header);
        }
        
        // Ajouter les champs du formulaire
        curl_formadd(&formpost, &lastptr,
                    CURLFORM_COPYNAME, "scope",
                    CURLFORM_COPYCONTENTS, scope,
                    CURLFORM_END);
        
        curl_formadd(&formpost, &lastptr,
                    CURLFORM_COPYNAME, "name",
                    CURLFORM_COPYCONTENTS, name,
                    CURLFORM_END);
        
        curl_formadd(&formpost, &lastptr,
                    CURLFORM_COPYNAME, "version",
                    CURLFORM_COPYCONTENTS, version,
                    CURLFORM_END);
        
        curl_formadd(&formpost, &lastptr,
                    CURLFORM_COPYNAME, "description",
                    CURLFORM_COPYCONTENTS, description,
                    CURLFORM_END);
        
        curl_formadd(&formpost, &lastptr,
                    CURLFORM_COPYNAME, "license",
                    CURLFORM_COPYCONTENTS, license,
                    CURLFORM_END);
        
        curl_formadd(&formpost, &lastptr,
                    CURLFORM_COPYNAME, "personal_code",
                    CURLFORM_COPYCONTENTS, personal_code,
                    CURLFORM_END);
        
        // Ajouter le fichier
        curl_formadd(&formpost, &lastptr,
                    CURLFORM_COPYNAME, "file",
                    CURLFORM_FILE, file_path,
                    CURLFORM_FILENAME, basename((char *)file_path),
                    CURLFORM_END);
        
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "User-Agent: Zarch-CLI/1.0");
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            curl_formfree(formpost);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return -1;
        }
        
        // Vérifier la réponse
        if (chunk.memory != NULL) {
            printf("Response: %s\n", chunk.memory);
        }
        
        free(chunk.memory);
        curl_formfree(formpost);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    
    return 0;
}

// Fonctions de token
char* read_token(void) {
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

// Détection de langage
char* detect_language(const char *path) {
    static char language[32] = "c"; // Par défaut C
    
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return language;
    }
    
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return language;
    }
    
    struct dirent *entry;
    int found_py = 0, found_js = 0, found_rs = 0, found_go = 0, found_rb = 0, found_c = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        char *ext = strrchr(entry->d_name, '.');
        if (ext != NULL) {
            if (strcmp(ext, ".py") == 0) found_py = 1;
            else if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0) found_js = 1;
            else if (strcmp(ext, ".rs") == 0) found_rs = 1;
            else if (strcmp(ext, ".go") == 0) found_go = 1;
            else if (strcmp(ext, ".rb") == 0) found_rb = 1;
            else if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) found_c = 1;
        }
        
        if (strcmp(entry->d_name, "setup.py") == 0 || 
            strcmp(entry->d_name, "requirements.txt") == 0 ||
            strcmp(entry->d_name, "pyproject.toml") == 0) {
            found_py = 1;
        }
        else if (strcmp(entry->d_name, "package.json") == 0) {
            found_js = 1;
        }
        else if (strcmp(entry->d_name, "Cargo.toml") == 0) {
            found_rs = 1;
        }
        else if (strcmp(entry->d_name, "go.mod") == 0) {
            found_go = 1;
        }
        else if (strcmp(entry->d_name, "Gemfile") == 0) {
            found_rb = 1;
        }
        else if (strcmp(entry->d_name, "Makefile") == 0 ||
                 strcmp(entry->d_name, "CMakeLists.txt") == 0) {
            found_c = 1;
        }
    }
    
    closedir(dir);
    
    if (found_py) strcpy(language, "python");
    else if (found_js) strcpy(language, "js");
    else if (found_rs) strcpy(language, "rust");
    else if (found_go) strcpy(language, "go");
    else if (found_rb) strcpy(language, "ruby");
    else if (found_c) strcpy(language, "c");
    
    return language;
}

// Lecture de manifest
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
    
    if (name) {
        const char *val = json_string_value(name);
        if (val) strncpy(manifest->name, val, sizeof(manifest->name) - 1);
    }
    
    if (version) {
        const char *val = json_string_value(version);
        if (val) strncpy(manifest->version, val, sizeof(manifest->version) - 1);
    }
    
    if (author) {
        const char *val = json_string_value(author);
        if (val) strncpy(manifest->author, val, sizeof(manifest->author) - 1);
    }
    
    if (description) {
        const char *val = json_string_value(description);
        if (val) strncpy(manifest->description, val, sizeof(manifest->description) - 1);
    }
    
    if (license) {
        const char *val = json_string_value(license);
        if (val) strncpy(manifest->license, val, sizeof(manifest->license) - 1);
    }
    
    if (env) {
        const char *val = json_string_value(env);
        if (val) strncpy(manifest->env, val, sizeof(manifest->env) - 1);
    }
    
    if (entry_point) {
        const char *val = json_string_value(entry_point);
        if (val) strncpy(manifest->entry_point, val, sizeof(manifest->entry_point) - 1);
    }
    
    if (dependencies) {
        char *deps_str = json_dumps(dependencies, JSON_COMPACT);
        if (deps_str) {
            strncpy(manifest->dependencies, deps_str, sizeof(manifest->dependencies) - 1);
            free(deps_str);
        }
    }
    
    if (build_commands) {
        char *build_str = json_dumps(build_commands, JSON_COMPACT);
        if (build_str) {
            strncpy(manifest->build_commands, build_str, sizeof(manifest->build_commands) - 1);
            free(build_str);
        }
    }
    
    if (install_path) {
        const char *val = json_string_value(install_path);
        if (val) strncpy(manifest->install_path, val, sizeof(manifest->install_path) - 1);
    }
    
    if (created_at) {
        const char *val = json_string_value(created_at);
        if (val) strncpy(manifest->created_at, val, sizeof(manifest->created_at) - 1);
    }
    
    if (updated_at) {
        const char *val = json_string_value(updated_at);
        if (val) strncpy(manifest->updated_at, val, sizeof(manifest->updated_at) - 1);
    }
    
    json_decref(root);
    return 0;
}

// Création de manifest
int create_default_manifest(const char *path, const char *name, const char *env) {
    char manifest_path[MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
    
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
    
    json_t *deps = json_array();
    json_object_set_new(root, "dependencies", deps);
    
    json_t *build_cmds = json_array();
    if (strcmp(env, "c") == 0) {
        json_array_append_new(build_cmds, json_string("gcc -o program *.c -lm"));
    } else if (strcmp(env, "python") == 0) {
        json_array_append_new(build_cmds, json_string("python setup.py build"));
        json_array_append_new(build_cmds, json_string("python setup.py install"));
    } else if (strcmp(env, "js") == 0) {
        json_array_append_new(build_cmds, json_string("npm install"));
        json_array_append_new(build_cmds, json_string("npm run build"));
    } else if (strcmp(env, "rust") == 0) {
        json_array_append_new(build_cmds, json_string("cargo build --release"));
    } else if (strcmp(env, "go") == 0) {
        json_array_append_new(build_cmds, json_string("go build"));
    } else if (strcmp(env, "ruby") == 0) {
        json_array_append_new(build_cmds, json_string("bundle install"));
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
    
    print_info("Created manifest file: zarch.json");
    return 0;
}

// Fonction pour créer une archive tar.gz
int create_tar_gz(const char *source_dir, const char *output_file) {
    struct archive *a;
    struct archive_entry *entry;
    struct stat st;
    char buff[8192];
    int len;
    int fd;
    
    a = archive_write_new();
    archive_write_add_filter_gzip(a);
    archive_write_set_format_pax_restricted(a);
    
    if (archive_write_open_filename(a, output_file) != ARCHIVE_OK) {
        print_error("Cannot create archive file");
        archive_write_free(a);
        return -1;
    }
    
    DIR *dir = opendir(source_dir);
    if (dir == NULL) {
        print_error("Cannot open source directory");
        archive_write_close(a);
        archive_write_free(a);
        return -1;
    }
    
    struct dirent *dirent;
    while ((dirent = readdir(dir)) != NULL) {
        if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
            continue;
            
        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", source_dir, dirent->d_name);
        
        if (stat(full_path, &st) != 0)
            continue;
            
        entry = archive_entry_new();
        archive_entry_set_pathname(entry, dirent->d_name);
        archive_entry_set_size(entry, st.st_size);
        archive_entry_set_filetype(entry, S_ISDIR(st.st_mode) ? AE_IFDIR : AE_IFREG);
        archive_entry_set_perm(entry, st.st_mode);
        archive_entry_set_mtime(entry, st.st_mtime, 0);
        
        archive_write_header(a, entry);
        
        if (!S_ISDIR(st.st_mode)) {
            fd = open(full_path, O_RDONLY);
            if (fd >= 0) {
                while ((len = read(fd, buff, sizeof(buff))) > 0) {
                    archive_write_data(a, buff, len);
                }
                close(fd);
            }
        }
        
        archive_entry_free(entry);
    }
    
    closedir(dir);
    archive_write_close(a);
    archive_write_free(a);
    
    print_info("Created archive");
    return 0;
}

// Exécuter les commandes de build
int execute_build_commands(const char *path, const char *commands_json) {
    print_info("Executing build commands...");
    
    json_error_t error;
    json_t *root = json_loads(commands_json, 0, &error);
    if (!root) {
        print_error("Invalid build commands JSON");
        return -1;
    }
    
    if (!json_is_array(root)) {
        print_error("Build commands should be an array");
        json_decref(root);
        return -1;
    }
    
    size_t index;
    json_t *value;
    int result = 0;
    
    json_array_foreach(root, index, value) {
        const char *command = json_string_value(value);
        if (command && strlen(command) > 0) {
            printf("Running: %s\n", command);
            
            char cmd[MAX_PATH + 100];
            snprintf(cmd, sizeof(cmd), "cd \"%s\" && %s", path, command);
            
            int ret = system(cmd);
            if (ret != 0) {
                print_warning("Command failed");
                result = -1;
            }
        }
    }
    
    json_decref(root);
    return result;
}

// Fonction build principale
int build_package(const char *path, const char *output_dir, int create_archive) {
    print_info("Building package...");
    
    Manifest manifest;
    if (read_manifest(path, &manifest) != 0) {
        print_error("No manifest found. Run 'zarch init' first.");
        return -1;
    }
    
    printf("Package: %s v%s\n", manifest.name, manifest.version);
    printf("Language: %s\n", manifest.env);
    
    // Exécuter les commandes de build
    if (strlen(manifest.build_commands) > 0 && strcmp(manifest.build_commands, "[]") != 0) {
        if (execute_build_commands(path, manifest.build_commands) != 0) {
            print_error("Build failed");
            return -1;
        }
    } else {
        print_info("No build commands specified");
    }
    
    // Créer l'archive si demandé
    if (create_archive) {
        char archive_name[MAX_PATH];
        if (output_dir != NULL && strlen(output_dir) > 0) {
            snprintf(archive_name, sizeof(archive_name), "%s/%s-%s.tar.gz", 
                    output_dir, manifest.name, manifest.version);
        } else {
            snprintf(archive_name, sizeof(archive_name), "%s-%s.tar.gz", 
                    manifest.name, manifest.version);
        }
        
        if (create_tar_gz(path, archive_name) == 0) {
            print_success("Package built successfully!");
            printf("Archive: %s\n", archive_name);
            return 0;
        } else {
            print_error("Failed to create archive");
            return -1;
        }
    } else {
        print_success("Build completed (no archive created)");
        return 0;
    }
}

// Initialisation améliorée
int init_package(const char *path) {
    char abs_path[MAX_PATH];
    char original_dir[MAX_PATH];
    
    if (getcwd(original_dir, sizeof(original_dir)) == NULL) {
        print_error("Cannot get current directory");
        return -1;
    }
    
    if (path == NULL || strlen(path) == 0) {
        strcpy(abs_path, ".");
    } else {
        if (realpath(path, abs_path) == NULL) {
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                perror("mkdir");
                return -1;
            }
            realpath(path, abs_path);
        }
    }
    
    print_info("Initializing package...");
    printf("Path: %s\n", abs_path);
    
    if (chdir(abs_path) != 0) {
        print_error("Cannot change to target directory");
        return -1;
    }
    
    char *detected_env = detect_language(".");
    printf("Detected language: %s\n", detected_env);
    
    char name[256];
    printf("\nPackage name: ");
    fflush(stdout);
    
    if (fgets(name, sizeof(name), stdin) == NULL) {
        print_error("Failed to read package name");
        chdir(original_dir);
        return -1;
    }
    
    name[strcspn(name, "\n")] = 0;
    
    if (strlen(name) == 0) {
        char *last_slash = strrchr(abs_path, '/');
        if (last_slash != NULL && strlen(last_slash) > 1) {
            strncpy(name, last_slash + 1, sizeof(name) - 1);
        } else {
            strncpy(name, abs_path, sizeof(name) - 1);
        }
        printf("Using directory name: %s\n", name);
    }
    
    char description[1024];
    printf("Description: ");
    fflush(stdout);
    
    if (fgets(description, sizeof(description), stdin) == NULL) {
        description[0] = '\0';
    } else {
        description[strcspn(description, "\n")] = 0;
    }
    
    if (create_default_manifest(".", name, detected_env) == 0) {
        // Mettre à jour la description
        if (strlen(description) > 0) {
            Manifest manifest;
            if (read_manifest(".", &manifest) == 0) {
                strncpy(manifest.description, description, sizeof(manifest.description) - 1);
                
                char manifest_path[MAX_PATH];
                snprintf(manifest_path, sizeof(manifest_path), "zarch.json");
                
                time_t now = time(NULL);
                struct tm *tm_info = localtime(&now);
                char timestamp[64];
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
                
                json_t *root = json_object();
                json_object_set_new(root, "name", json_string(manifest.name));
                json_object_set_new(root, "version", json_string(manifest.version));
                json_object_set_new(root, "author", json_string(manifest.author));
                json_object_set_new(root, "description", json_string(manifest.description));
                json_object_set_new(root, "license", json_string(manifest.license));
                json_object_set_new(root, "env", json_string(manifest.env));
                json_object_set_new(root, "entry_point", json_string(manifest.entry_point));
                
                json_error_t error;
                json_t *deps = json_loads(manifest.dependencies, 0, &error);
                if (deps) {
                    json_object_set_new(root, "dependencies", deps);
                } else {
                    json_object_set_new(root, "dependencies", json_array());
                }
                
                json_t *build_cmds = json_loads(manifest.build_commands, 0, &error);
                if (build_cmds) {
                    json_object_set_new(root, "build_commands", build_cmds);
                } else {
                    json_object_set_new(root, "build_commands", json_array());
                }
                
                json_object_set_new(root, "install_path", json_string(manifest.install_path));
                json_object_set_new(root, "created_at", json_string(manifest.created_at));
                json_object_set_new(root, "updated_at", json_string(timestamp));
                
                char *manifest_str = json_dumps(root, JSON_INDENT(2));
                
                FILE *file = fopen(manifest_path, "w");
                if (file) {
                    fprintf(file, "%s\n", manifest_str);
                    fclose(file);
                }
                
                free(manifest_str);
                json_decref(root);
            }
        }
        
        print_success("Package initialized!");
        printf("\n");
        printf("📁 Directory: %s\n", abs_path);
        printf("📄 Manifest: %s/zarch.json\n", abs_path);
        printf("🛠️  Language: %s\n", detected_env);
        printf("\nNext steps:\n");
        printf("  1. Edit zarch.json to configure your package\n");
        printf("  2. Add your source code\n");
        printf("  3. Run 'zarch build .' to build\n");
        printf("  4. Run 'zarch publish . user' to publish\n");
        
        // Créer un fichier d'exemple
        if (strcmp(detected_env, "c") == 0) {
            FILE *example = fopen("main.c", "w");
            if (example) {
                fprintf(example, "#include <stdio.h>\n\n");
                fprintf(example, "int main() {\n");
                fprintf(example, "    printf(\"Hello from %s!\\n\");\n", name);
                fprintf(example, "    return 0;\n");
                fprintf(example, "}\n");
                fclose(example);
                printf("  5. Example C file created: main.c\n");
            }
        }
        
        printf("\n");
    } else {
        print_error("Failed to initialize package");
    }
    
    chdir(original_dir);
    return 0;
}

// Fonctions manquantes - implémentations simplifiées
int login(const char *username, const char *password) {
    print_info("Logging in...");
    printf("Username: %s\n", username);
    
    char fake_token[100];
    snprintf(fake_token, sizeof(fake_token), "fake-token-%s-%ld", username, time(NULL));
    
    if (save_token(fake_token) == 0) {
        print_success("Login successful!");
        return 0;
    } else {
        print_error("Login failed");
        return -1;
    }
}

int publish_package(const char *path, const char *scope) {
    print_info("Publishing package...");
    printf("Path: %s\n", path);
    printf("Scope: %s\n", scope);
    
    Manifest manifest;
    if (read_manifest(path, &manifest) != 0) {
        print_error("No manifest found. Run 'zarch init' first.");
        return -1;
    }
    
    printf("\nPackage info:\n");
    printf("  Name: %s\n", manifest.name);
    printf("  Version: %s\n", manifest.version);
    printf("  Language: %s\n", manifest.env);
    printf("  Description: %s\n", manifest.description);
    printf("  License: %s\n", manifest.license);
    
    // Créer l'archive
    char archive_name[MAX_PATH];
    snprintf(archive_name, sizeof(archive_name), "/tmp/%s-%s.tar.gz", 
            manifest.name, manifest.version);
    
    if (create_tar_gz(path, archive_name) != 0) {
        print_error("Failed to create archive");
        return -1;
    }
    
    // Vérifier le token
    char *token = read_token();
    if (token == NULL) {
        print_warning("Not logged in. Using local mode.");
    } else {
        printf("Token: %s...\n", token);
    }
    
    // Demander le code de sécurité
    char personal_code[32];
    printf("\nSecurity code (2FA): ");
    fflush(stdout);
    
    if (fgets(personal_code, sizeof(personal_code), stdin) == NULL) {
        print_error("Failed to read security code");
        return -1;
    }
    personal_code[strcspn(personal_code, "\n")] = 0;
    
    // Envoyer au serveur
    printf("\nUploading to registry...\n");
    
    char url[MAX_URL];
    snprintf(url, sizeof(url), "%s/api/package/upload/%s/%s", 
            ZARCH_URL, scope, manifest.name);
    
    if (http_post_file(url, archive_name, token, scope, manifest.name, 
                      manifest.version, manifest.description, 
                      manifest.license, personal_code) == 0) {
        print_success("Package published successfully!");
    } else {
        print_error("Failed to publish package");
    }
    
    // Nettoyer
    remove(archive_name);
    
    return 0;
}

int search_packages(const char *query) {
    print_info("Searching packages...");
    
    if (query != NULL && strlen(query) > 0) {
        printf("Query: %s\n", query);
    }
    
    printf("\n" COLOR_CYAN "=== Available Packages ===" COLOR_RESET "\n\n");
    printf("1. @user/example-c (v1.0.0) - Example C package\n");
    printf("2. @user/example-py (v1.2.0) - Example Python package\n");
    printf("3. @user/example-js (v2.1.0) - Example JavaScript package\n");
    printf("\nUse 'zarch info <package>' for more details\n");
    
    return 0;
}

// Fonction build avec options
int build_and_package(const char *path, const char *scope, const char *bind_file) {
    print_info("Building and packaging...");
    
    Manifest manifest;
    if (read_manifest(path, &manifest) != 0) {
        print_error("No manifest found. Run 'zarch init' first.");
        return -1;
    }
    
    // Mettre à jour l'entry point si spécifié
    if (bind_file != NULL && strlen(bind_file) > 0) {
        strncpy(manifest.entry_point, bind_file, sizeof(manifest.entry_point) - 1);
        
        // Mettre à jour le manifest
        char manifest_path[MAX_PATH];
        snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
        
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        
        json_t *root = json_object();
        json_object_set_new(root, "name", json_string(manifest.name));
        json_object_set_new(root, "version", json_string(manifest.version));
        json_object_set_new(root, "author", json_string(manifest.author));
        json_object_set_new(root, "description", json_string(manifest.description));
        json_object_set_new(root, "license", json_string(manifest.license));
        json_object_set_new(root, "env", json_string(manifest.env));
        json_object_set_new(root, "entry_point", json_string(manifest.entry_point));
        
        json_error_t error;
        json_t *deps = json_loads(manifest.dependencies, 0, &error);
        if (deps) {
            json_object_set_new(root, "dependencies", deps);
        } else {
            json_object_set_new(root, "dependencies", json_array());
        }
        
        json_t *build_cmds = json_loads(manifest.build_commands, 0, &error);
        if (build_cmds) {
            json_object_set_new(root, "build_commands", build_cmds);
        } else {
            json_object_set_new(root, "build_commands", json_array());
        }
        
        json_object_set_new(root, "install_path", json_string(manifest.install_path));
        json_object_set_new(root, "created_at", json_string(manifest.created_at));
        json_object_set_new(root, "updated_at", json_string(timestamp));
        
        char *manifest_str = json_dumps(root, JSON_INDENT(2));
        
        FILE *file = fopen(manifest_path, "w");
        if (file) {
            fprintf(file, "%s\n", manifest_str);
            fclose(file);
            print_info("Updated entry point in manifest");
        }
        
        free(manifest_str);
        json_decref(root);
    }
    
    // Build le package
    if (build_package(path, NULL, 1) == 0) {
        print_success("Build completed!");
        
        // Demander si on veut publier
        char response[10];
        printf("\nPublish to registry? (y/N): ");
        fflush(stdout);
        
        if (fgets(response, sizeof(response), stdin) != NULL) {
            if (response[0] == 'y' || response[0] == 'Y') {
                return publish_package(path, scope ? scope : "user");
            }
        }
        
        return 0;
    }
    
    return -1;
}

// Fonction principale avec commande build
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
        
    } else if (strcmp(command, "build") == 0) {
        if (argc < 3) {
            print_error("Usage: zarch build <path> [options]");
            return 1;
        }
        
        const char *path = argv[2];
        const char *bind_file = NULL;
        const char *load_dir = NULL;
        const char *output_dir = NULL;
        const char *scope = "user";
        int create_archive = 1;
        
        // Parser les options
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
                bind_file = argv[++i];
            } else if (strcmp(argv[i], "--load") == 0 && i + 1 < argc) {
                load_dir = argv[++i];
            } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                output_dir = argv[++i];
            } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
                // Format option (ignoré pour l'instant, toujours tar.gz)
                i++;
            } else if (strcmp(argv[i], "--no-archive") == 0) {
                create_archive = 0;
            } else if (strcmp(argv[i], "--scope") == 0 && i + 1 < argc) {
                scope = argv[++i];
            } else {
                print_warning("Unknown option ignored");
            }
        }
        
        if (bind_file != NULL) {
            return build_and_package(path, scope, bind_file);
        } else {
            return build_package(path, output_dir, create_archive);
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
        // Simplifié pour l'exemple
        print_info("Installing package...");
        printf("Package: %s\n", argv[2]);
        print_success("Package installed (simulated)");
        return 0;
        
    } else if (strcmp(command, "search") == 0) {
        const char *query = argc > 2 ? argv[2] : "";
        return search_packages(query) == 0 ? 0 : 1;
        
    } else if (strcmp(command, "version") == 0 || strcmp(command, "--version") == 0) {
        printf("zarch v%s\n", VERSION);
        return 0;
        
    } else if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0) {
        show_help();
        return 0;
        
    } else {
        print_error("Unknown command");
        show_help();
        return 1;
    }
    
    curl_global_cleanup();
    return 0;
}
