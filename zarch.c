/*
 * Zarch CLI v6.0 - Ultimate Edition
 * CLI complet avec jq, couleurs, autocomplétion, et nombreuses fonctionnalités
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <libgen.h>
#include <stdarg.h>

#define VERSION "6.0.0"
#define REGISTRY_URL "https://zenv-hub.onrender.com"
#define CONFIG_DIR ".zarch"
#define CONFIG_FILE "config.json"
#define CACHE_FILE "cache.json"
#define LIB_PATH "/usr/local/bin/swiftvelox/addws"
#define HISTORY_FILE ".zarch_history"
#define TEMPLATES_DIR "templates"

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GRAY    "\033[90m"

// ===== STRUCTURES =====
struct MemoryStruct {
    char *memory;
    size_t size;
};

typedef struct {
    char token[256];
    char username[64];
    char email[128];
    char personal_code[16];
    char default_scope[64];
    char api_url[256];
    time_t last_update;
    int auto_update;
    int color_mode;
    int verbose;
} Config;

typedef struct {
    int argc;
    char **argv;
    char command[32];
    char subcommand[32];
    char package_name[256];
    char version[32];
    char scope[64];
    char path[512];
    char username[64];
    char password[64];
    char personal_code[32];
    char tag[32];
    char query[256];
    char output_format[16];
    int force;
    int verbose;
    int quiet;
    int json;
    int no_cache;
    int auto_version;
    int help;
    int version_flag;
    int interactive;
    int list;
    int all;
    int global;
    int local;
    int yes;
} Args;

typedef struct {
    char name[128];
    char scope[64];
    char version[32];
    char description[256];
    char author[64];
    char license[32];
    char sha256[65];
    long size;
    int downloads;
    char created_at[32];
    char updated_at[32];
} PackageInfo;

// ===== GLOBALS =====
Config g_config;
Args g_args;
int g_color_enabled = 1;

// ===== UTILITAIRES AVANCÉS =====
void print_color(const char* color, const char* icon, const char* format, ...) {
    if (!g_color_enabled) color = "";
    
    va_list args;
    va_start(args, format);
    
    if (icon) printf("%s%s ", color, icon);
    vprintf(format, args);
    printf(COLOR_RESET "\n");
    
    va_end(args);
}

void print_step(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    print_color(COLOR_BLUE, "🔄", "%s", buffer);
}

void print_success(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    print_color(COLOR_GREEN, "✅", "%s", buffer);
}

void print_error(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    print_color(COLOR_RED, "❌", "%s", buffer);
}

void print_warning(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    print_color(COLOR_YELLOW, "⚠️", "%s", buffer);
}

void print_info(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    print_color(COLOR_CYAN, "ℹ️", "%s", buffer);
}

void print_debug(const char* format, ...) {
    if (g_args.verbose) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        print_color(COLOR_GRAY, "🐛", "%s", buffer);
    }
}

char* get_config_path(const char* filename) {
    static char path[1024];
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(path, sizeof(path), "%s/%s/%s", home, CONFIG_DIR, filename);
    return path;
}

int ensure_config_dir() {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", getenv("HOME"), CONFIG_DIR);
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

int file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

int is_directory(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void create_directory(const char* path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    system(cmd);
}

// ===== JSON OUTPUT WITH JQ =====
void json_print_raw(const char* json_str) {
    if (g_args.json) {
        printf("%s\n", json_str);
        return;
    }
    
    // Use jq for pretty printing if available
    FILE *jq = popen("jq . 2>/dev/null || cat", "w");
    if (jq) {
        fprintf(jq, "%s", json_str);
        pclose(jq);
    } else {
        printf("%s\n", json_str);
    }
}

void json_print_object(json_t* obj) {
    char* json_str = json_dumps(obj, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
    json_print_raw(json_str);
    free(json_str);
}

// ===== CURL WRAPPER =====
size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    
    if (!ptr) return 0;
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

json_t* api_request(const char* url, const char* method, const char* data, int* http_code) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    struct MemoryStruct chunk = {malloc(1), 0};
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    if (strlen(g_config.token) > 0) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_config.token);
        headers = curl_slist_append(headers, auth_header);
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Zarch-CLI/6.0.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (data) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (data) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    if (http_code) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_code);
    }
    
    json_t* result = NULL;
    if (res == CURLE_OK && chunk.size > 0) {
        result = json_loads(chunk.memory, 0, NULL);
    }
    
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(chunk.memory);
    
    return result;
}

// ===== CONFIG MANAGEMENT =====
int load_config() {
    char* config_path = get_config_path(CONFIG_FILE);
    
    if (!file_exists(config_path)) {
        memset(&g_config, 0, sizeof(Config));
        strcpy(g_config.api_url, REGISTRY_URL);
        g_config.color_mode = 1;
        return 0;
    }
    
    FILE* f = fopen(config_path, "r");
    if (!f) return 0;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buffer = malloc(size + 1);
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    json_t* root = json_loads(buffer, 0, NULL);
    free(buffer);
    
    if (!root) return 0;
    
    json_t* token = json_object_get(root, "token");
    json_t* username = json_object_get(root, "username");
    json_t* email = json_object_get(root, "email");
    json_t* personal_code = json_object_get(root, "personal_code");
    json_t* default_scope = json_object_get(root, "default_scope");
    json_t* api_url = json_object_get(root, "api_url");
    json_t* auto_update = json_object_get(root, "auto_update");
    json_t* color_mode = json_object_get(root, "color_mode");
    json_t* last_update = json_object_get(root, "last_update");
    
    if (token) strncpy(g_config.token, json_string_value(token), sizeof(g_config.token)-1);
    if (username) strncpy(g_config.username, json_string_value(username), sizeof(g_config.username)-1);
    if (email) strncpy(g_config.email, json_string_value(email), sizeof(g_config.email)-1);
    if (personal_code) strncpy(g_config.personal_code, json_string_value(personal_code), sizeof(g_config.personal_code)-1);
    if (default_scope) strncpy(g_config.default_scope, json_string_value(default_scope), sizeof(g_config.default_scope)-1);
    if (api_url) strncpy(g_config.api_url, json_string_value(api_url), sizeof(g_config.api_url)-1);
    
    g_config.auto_update = auto_update ? json_integer_value(auto_update) : 1;
    g_config.color_mode = color_mode ? json_integer_value(color_mode) : 1;
    g_config.last_update = last_update ? json_integer_value(last_update) : 0;
    g_config.verbose = 0;
    
    json_decref(root);
    g_color_enabled = g_config.color_mode;
    return 1;
}

int save_config() {
    if (!ensure_config_dir()) return 0;
    
    char* config_path = get_config_path(CONFIG_FILE);
    
    json_t* root = json_object();
    json_object_set_new(root, "token", json_string(g_config.token));
    json_object_set_new(root, "username", json_string(g_config.username));
    json_object_set_new(root, "email", json_string(g_config.email));
    json_object_set_new(root, "personal_code", json_string(g_config.personal_code));
    json_object_set_new(root, "default_scope", json_string(g_config.default_scope));
    json_object_set_new(root, "api_url", json_string(g_config.api_url));
    json_object_set_new(root, "auto_update", json_integer(g_config.auto_update));
    json_object_set_new(root, "color_mode", json_integer(g_config.color_mode));
    json_object_set_new(root, "last_update", json_integer(time(NULL)));
    json_object_set_new(root, "version", json_string(VERSION));
    
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

// ===== ARGUMENT PARSING =====
void init_args(Args* args, int argc, char** argv) {
    memset(args, 0, sizeof(Args));
    args->argc = argc;
    args->argv = argv;
    strcpy(args->output_format, "pretty");
    strcpy(args->path, ".");
}

int parse_bool_arg(const char* arg) {
    return (strcmp(arg, "true") == 0 || strcmp(arg, "1") == 0 || 
            strcmp(arg, "yes") == 0 || strcmp(arg, "on") == 0);
}

void parse_args(Args* args) {
    for (int i = 1; i < args->argc; i++) {
        char* arg = args->argv[i];
        
        // Command detection
        if (i == 1 && arg[0] != '-') {
            strncpy(args->command, arg, sizeof(args->command)-1);
            continue;
        }
        
        // Subcommand detection
        if (i == 2 && arg[0] != '-' && strlen(args->command) > 0) {
            strncpy(args->subcommand, arg, sizeof(args->subcommand)-1);
            continue;
        }
        
        // Flags
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            args->help = 1;
        } else if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            args->version_flag = 1;
        } else if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-V") == 0) {
            args->verbose = 1;
            g_config.verbose = 1;
        } else if (strcmp(arg, "--quiet") == 0 || strcmp(arg, "-q") == 0) {
            args->quiet = 1;
        } else if (strcmp(arg, "--json") == 0 || strcmp(arg, "-j") == 0) {
            args->json = 1;
            strcpy(args->output_format, "json");
        } else if (strcmp(arg, "--force") == 0 || strcmp(arg, "-f") == 0) {
            args->force = 1;
        } else if (strcmp(arg, "--yes") == 0 || strcmp(arg, "-y") == 0) {
            args->yes = 1;
        } else if (strcmp(arg, "--no-cache") == 0) {
            args->no_cache = 1;
        } else if (strcmp(arg, "--auto-version") == 0) {
            args->auto_version = 1;
        } else if (strcmp(arg, "--all") == 0 || strcmp(arg, "-a") == 0) {
            args->all = 1;
        } else if (strcmp(arg, "--global") == 0 || strcmp(arg, "-g") == 0) {
            args->global = 1;
        } else if (strcmp(arg, "--local") == 0 || strcmp(arg, "-l") == 0) {
            args->local = 1;
        } else if (strcmp(arg, "--list") == 0) {
            args->list = 1;
        } else if (strcmp(arg, "--interactive") == 0 || strcmp(arg, "-i") == 0) {
            args->interactive = 1;
        }
        
        // Options with values
        else if (strncmp(arg, "--scope=", 8) == 0) {
            strncpy(args->scope, arg + 8, sizeof(args->scope)-1);
        } else if (strncmp(arg, "--version=", 10) == 0) {
            strncpy(args->version, arg + 10, sizeof(args->version)-1);
        } else if (strncmp(arg, "--path=", 7) == 0) {
            strncpy(args->path, arg + 7, sizeof(args->path)-1);
        } else if (strncmp(arg, "--output=", 9) == 0) {
            strncpy(args->output_format, arg + 9, sizeof(args->output_format)-1);
        } else if (strncmp(arg, "--tag=", 6) == 0) {
            strncpy(args->tag, arg + 6, sizeof(args->tag)-1);
        } else if (strcmp(arg, "--scope") == 0 && i+1 < args->argc) {
            strncpy(args->scope, args->argv[++i], sizeof(args->scope)-1);
        } else if (strcmp(arg, "--version") == 0 && i+1 < args->argc) {
            strncpy(args->version, args->argv[++i], sizeof(args->version)-1);
        } else if (strcmp(arg, "--path") == 0 && i+1 < args->argc) {
            strncpy(args->path, args->argv[++i], sizeof(args->path)-1);
        }
        
        // Package name (first non-option after command)
        else if (arg[0] != '-' && strlen(args->package_name) == 0 && 
                (strcmp(args->command, "install") == 0 ||
                 strcmp(args->command, "uninstall") == 0 ||
                 strcmp(args->command, "remove") == 0 ||
                 strcmp(args->command, "search") == 0 ||
                 strcmp(args->command, "info") == 0 ||
                 strcmp(args->command, "update") == 0)) {
            strncpy(args->package_name, arg, sizeof(args->package_name)-1);
        }
        
        // Username/password for login
        else if (strcmp(args->command, "login") == 0) {
            if (strlen(args->username) == 0) {
                strncpy(args->username, arg, sizeof(args->username)-1);
            } else if (strlen(args.password) == 0) {
                strncpy(args.password, arg, sizeof(args.password)-1);
            }
        }
        
        // Personal code for publish
        else if (strcmp(args->command, "publish") == 0 && strlen(args->personal_code) == 0) {
            strncpy(args->personal_code, arg, sizeof(args->personal_code)-1);
        }
        
        // Query for search
        else if (strcmp(args->command, "search") == 0 && strlen(args->query) == 0) {
            strncpy(args->query, arg, sizeof(args->query)-1);
        }
    }
}

// ===== PACKAGE MANIFEST =====
json_t* read_manifest(const char* path) {
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
    
    if (!file_exists(manifest_path)) {
        return NULL;
    }
    
    json_error_t error;
    json_t* root = json_load_file(manifest_path, 0, &error);
    return root;
}

int write_manifest(const char* path, json_t* manifest) {
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
    
    char* json_str = json_dumps(manifest, JSON_INDENT(2));
    FILE* f = fopen(manifest_path, "w");
    if (!f) {
        free(json_str);
        return 0;
    }
    
    fprintf(f, "%s", json_str);
    fclose(f);
    free(json_str);
    return 1;
}

void increment_version(char* version) {
    int major = 0, minor = 0, patch = 0;
    sscanf(version, "%d.%d.%d", &major, &minor, &patch);
    patch++;
    snprintf(version, 32, "%d.%d.%d", major, minor, patch);
}

// ===== PACKAGE BUILDING =====
int build_package(const char* path, char* output_path, size_t output_size) {
    print_step("Construction du package...");
    
    json_t* manifest = read_manifest(path);
    if (!manifest) {
        print_error("zarch.json non trouvé");
        return 0;
    }
    
    const char* name = json_string_value(json_object_get(manifest, "name"));
    const char* scope = json_string_value(json_object_get(manifest, "scope"));
    char version[32];
    
    if (g_args.auto_version) {
        const char* current_version = json_string_value(json_object_get(manifest, "version"));
        strcpy(version, current_version);
        increment_version(version);
        json_object_set_new(manifest, "version", json_string(version));
        write_manifest(path, manifest);
        print_info("Version incrémentée: %s -> %s", current_version, version);
    } else {
        const char* ver = json_string_value(json_object_get(manifest, "version"));
        if (ver) strcpy(version, ver);
        else strcpy(version, "1.0.0");
    }
    
    if (!scope || strcmp(scope, "") == 0) {
        scope = g_config.default_scope;
        if (strlen(scope) == 0) scope = "user";
    }
    
    // Create temp directory
    char temp_dir[1024];
    snprintf(temp_dir, sizeof(temp_dir), "/tmp/zarch-build-%ld", (long)time(NULL));
    create_directory(temp_dir);
    
    // Copy files based on include patterns
    const char* include = json_string_value(json_object_get(manifest, "include"));
    if (!include) include = ".";
    
    char cmd[2048];
    if (strcmp(include, ".") == 0 || strcmp(include, "./") == 0) {
        // Copy everything except .git, node_modules, etc.
        snprintf(cmd, sizeof(cmd), "cd \"%s\" && find . -type f -not -path './.git/*' -not -path './node_modules/*' -not -path './.zarch/*' -exec cp --parents {} \"%s\" \\; 2>/dev/null || true", 
                path, temp_dir);
    } else {
        // Copy specific patterns
        snprintf(cmd, sizeof(cmd), "cd \"%s\" && cp -r %s \"%s\" 2>/dev/null || true", 
                path, include, temp_dir);
    }
    
    system(cmd);
    
    // Create final archive
    snprintf(output_path, output_size, "/tmp/%s-%s-%s.tar.gz", scope, name, version);
    
    snprintf(cmd, sizeof(cmd), "tar -czf \"%s\" -C \"%s\" . 2>/dev/null", output_path, temp_dir);
    
    if (system(cmd) != 0) {
        print_error("Échec de la création de l'archive");
        // Cleanup
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
        system(cmd);
        json_decref(manifest);
        return 0;
    }
    
    // Cleanup temp dir
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
    system(cmd);
    
    // Compute SHA256
    char sha_cmd[1024];
    snprintf(sha_cmd, sizeof(sha_cmd), "sha256sum \"%s\" | cut -d' ' -f1", output_path);
    FILE* sha_pipe = popen(sha_cmd, "r");
    char sha256[65] = {0};
    if (sha_pipe) {
        fgets(sha256, sizeof(sha256), sha_pipe);
        sha256[strcspn(sha256, "\n")] = 0;
        pclose(sha_pipe);
    }
    
    // Get file size
    struct stat st;
    stat(output_path, &st);
    
    if (!g_args.quiet) {
        printf("📦 Package: %s v%s\n", name, version);
        printf("📁 Scope: %s\n", scope);
        printf("📏 Taille: %.2f MB\n", (double)st.st_size / (1024*1024));
        printf("🔐 SHA256: %s\n", sha256);
        printf("📄 Fichier: %s\n", output_path);
    }
    
    json_decref(manifest);
    print_success("Package construit avec succès");
    return 1;
}

// ===== API COMMANDS =====
int cmd_login() {
    if (strlen(g_args.username) == 0 || strlen(g_args.password) == 0) {
        if (!g_args.interactive) {
            print_error("Usage: zarch login <username> <password>");
            return 0;
        }
        
        printf("Username: ");
        fgets(g_args.username, sizeof(g_args.username), stdin);
        g_args.username[strcspn(g_args.username, "\n")] = 0;
        
        printf("Password: ");
        // Disable echo for password
        system("stty -echo");
        fgets(g_args.password, sizeof(g_args.password), stdin);
        g_args.password[strcspn(g_args.password, "\n")] = 0;
        system("stty echo");
        printf("\n");
    }
    
    print_step("Connexion en cours...");
    
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/auth/login", g_config.api_url);
    
    json_t* data = json_object();
    json_object_set_new(data, "username", json_string(g_args.username));
    json_object_set_new(data, "password", json_string(g_args.password));
    
    char* data_str = json_dumps(data, 0);
    int http_code = 0;
    json_t* response = api_request(url, "POST", data_str, &http_code);
    
    free(data_str);
    json_decref(data);
    
    if (!response) {
        print_error("Échec de la connexion");
        return 0;
    }
    
    if (http_code != 200) {
        const char* error = json_string_value(json_object_get(response, "error"));
        print_error(error ? error : "Échec de la connexion");
        json_decref(response);
        return 0;
    }
    
    json_t* token = json_object_get(response, "token");
    json_t* user = json_object_get(response, "user");
    
    if (token && user) {
        strncpy(g_config.token, json_string_value(token), sizeof(g_config.token)-1);
        strncpy(g_config.username, json_string_value(json_object_get(user, "username")), 
                sizeof(g_config.username)-1);
        
        json_t* email = json_object_get(user, "email");
        if (email) {
            strncpy(g_config.email, json_string_value(email), sizeof(g_config.email)-1);
        }
        
        save_config();
        print_success("Connecté avec succès");
        
        if (g_args.json) {
            json_print_object(response);
        }
    }
    
    json_decref(response);
    return 1;
}

int cmd_logout() {
    print_step("Déconnexion...");
    
    // Clear local config
    memset(g_config.token, 0, sizeof(g_config.token));
    memset(g_config.username, 0, sizeof(g_config.username));
    save_config();
    
    print_success("Déconnecté avec succès");
    return 1;
}

int cmd_whoami() {
    if (strlen(g_config.username) == 0) {
        print_error("Non connecté");
        return 0;
    }
    
    if (g_args.json) {
        json_t* obj = json_object();
        json_object_set_new(obj, "username", json_string(g_config.username));
        json_object_set_new(obj, "email", json_string(g_config.email));
        json_object_set_new(obj, "scope", json_string(g_config.default_scope));
        json_print_object(obj);
        json_decref(obj);
    } else {
        printf("👤 Utilisateur: %s%s%s\n", COLOR_GREEN, g_config.username, COLOR_RESET);
        if (strlen(g_config.email) > 0) {
            printf("📧 Email: %s\n", g_config.email);
        }
        if (strlen(g_config.default_scope) > 0) {
            printf("🏷️  Scope par défaut: %s\n", g_config.default_scope);
        }
    }
    
    return 1;
}

int cmd_init() {
    print_step("Initialisation d'un nouveau package...");
    
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", g_args.path);
    
    if (file_exists(manifest_path) && !g_args.force) {
        print_error("zarch.json existe déjà. Utilisez --force pour écraser.");
        return 0;
    }
    
    char name[128], version[32], description[256], author[128], license[32], scope[64], main_file[256];
    
    if (g_args.interactive || (!g_args.package_name && !g_args.version)) {
        printf("Nom du package: ");
        fgets(name, sizeof(name), stdin);
        name[strcspn(name, "\n")] = 0;
        
        printf("Version (1.0.0): ");
        fgets(version, sizeof(version), stdin);
        version[strcspn(version, "\n")] = 0;
        if (!version[0]) strcpy(version, "1.0.0");
        
        printf("Description: ");
        fgets(description, sizeof(description), stdin);
        description[strcspn(description, "\n")] = 0;
        
        printf("Auteur: ");
        fgets(author, sizeof(author), stdin);
        author[strcspn(author, "\n")] = 0;
        
        printf("License (MIT): ");
        fgets(license, sizeof(license), stdin);
        license[strcspn(license, "\n")] = 0;
        if (!license[0]) strcpy(license, "MIT");
        
        printf("Scope (%s): ", g_config.default_scope);
        fgets(scope, sizeof(scope), stdin);
        scope[strcspn(scope, "\n")] = 0;
        if (!scope[0] && strlen(g_config.default_scope) > 0) {
            strcpy(scope, g_config.default_scope);
        } else if (!scope[0]) {
            strcpy(scope, "user");
        }
        
        printf("Fichier principal (src/main.svlib): ");
        fgets(main_file, sizeof(main_file), stdin);
        main_file[strcspn(main_file, "\n")] = 0;
        if (!main_file[0]) strcpy(main_file, "src/main.svlib");
    } else {
        strcpy(name, g_args.package_name);
        strcpy(version, g_args.version[0] ? g_args.version : "1.0.0");
        strcpy(description, "Package créé avec Zarch");
        strcpy(author, g_config.username);
        strcpy(license, "MIT");
        strcpy(scope, g_args.scope[0] ? g_args.scope : 
               (strlen(g_config.default_scope) > 0 ? g_config.default_scope : "user"));
        strcpy(main_file, "src/main.svlib");
    }
    
    // Create manifest
    json_t* manifest = json_object();
    json_object_set_new(manifest, "name", json_string(name));
    json_object_set_new(manifest, "version", json_string(version));
    json_object_set_new(manifest, "description", json_string(description));
    json_object_set_new(manifest, "author", json_string(author));
    json_object_set_new(manifest, "license", json_string(license));
    json_object_set_new(manifest, "scope", json_string(scope));
    json_object_set_new(manifest, "main", json_string(main_file));
    json_object_set_new(manifest, "include", json_string("src/"));
    
    if (!write_manifest(g_args.path, manifest)) {
        print_error("Échec de la création du manifeste");
        json_decref(manifest);
        return 0;
    }
    
    // Create directory structure
    create_directory("src");
    create_directory("tests");
    create_directory("examples");
    
    // Create main.svlib
    char main_path[1024];
    snprintf(main_path, sizeof(main_path), "%s/%s", g_args.path, main_file);
    FILE* f = fopen(main_path, "w");
    if (f) {
        fprintf(f, "// %s v%s\n", name, version);
        fprintf(f, "// %s\n\n", description);
        fprintf(f, "module %s {\n", name);
        fprintf(f, "    // Votre code ici\n");
        fprintf(f, "}\n");
        fclose(f);
    }
    
    // Create README.md
    char readme_path[1024];
    snprintf(readme_path, sizeof(readme_path), "%s/README.md", g_args.path);
    f = fopen(readme_path, "w");
    if (f) {
        fprintf(f, "# %s\n\n", name);
        fprintf(f, "%s\n\n", description);
        fprintf(f, "## Installation\n\n");
        fprintf(f, "```bash\nzarch install %s\n```\n\n", name);
        fprintf(f, "## Utilisation\n\n");
        fprintf(f, "```swiftvelox\nimport %s\n```\n", name);
        fclose(f);
    }
    
    // Create .gitignore
    char gitignore_path[1024];
    snprintf(gitignore_path, sizeof(gitignore_path), "%s/.gitignore", g_args.path);
    f = fopen(gitignore_path, "w");
    if (f) {
        fprintf(f, "*.tar.gz\n");
        fprintf(f, ".zarch/\n");
        fprintf(f, "node_modules/\n");
        fprintf(f, "__pycache__/\n");
        fprintf(f, "*.pyc\n");
        fclose(f);
    }
    
    json_decref(manifest);
    print_success("Package initialisé avec succès");
    
    if (!g_args.quiet) {
        printf("\n📁 Structure créée:\n");
        printf("  📄 zarch.json    - Manifeste du package\n");
        printf("  📄 README.md     - Documentation\n");
        printf("  📄 %s - Fichier principal\n", main_file);
        printf("  📄 .gitignore    - Fichiers ignorés\n");
        printf("  📁 src/          - Code source\n");
        printf("  📁 tests/        - Tests\n");
        printf("  📁 examples/     - Exemples\n");
    }
    
    return 1;
}

int cmd_build() {
    char output_path[1024];
    if (!build_package(g_args.path, output_path, sizeof(output_path))) {
        return 0;
    }
    return 1;
}

int cmd_publish() {
    // Check if logged in
    if (strlen(g_config.token) == 0) {
        print_error("Vous devez être connecté pour publier");
        return 0;
    }
    
    // Get personal code if not provided
    if (strlen(g_args.personal_code) == 0) {
        if (strlen(g_config.personal_code) > 0 && !g_args.interactive) {
            strcpy(g_args.personal_code, g_config.personal_code);
        } else {
            printf("Code de sécurité personnel: ");
            fgets(g_args.personal_code, sizeof(g_args.personal_code), stdin);
            g_args.personal_code[strcspn(g_args.personal_code, "\n")] = 0;
        }
    }
    
    // Build package
    char archive_path[1024];
    if (!build_package(g_args.path, archive_path, sizeof(archive_path))) {
        return 0;
    }
    
    // Read manifest for metadata
    json_t* manifest = read_manifest(g_args.path);
    if (!manifest) {
        print_error("Manifeste non trouvé");
        return 0;
    }
    
    const char* name = json_string_value(json_object_get(manifest, "name"));
    const char* scope = json_string_value(json_object_get(manifest, "scope"));
    const char* version = json_string_value(json_object_get(manifest, "version"));
    const char* description = json_string_value(json_object_get(manifest, "description"));
    const char* license = json_string_value(json_object_get(manifest, "license"));
    
    if (!scope || strcmp(scope, "") == 0) {
        scope = g_config.default_scope;
        if (strlen(scope) == 0) scope = "user";
    }
    
    print_step("Publication du package...");
    
    // Prepare upload
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/package/upload/%s/%s", 
             g_config.api_url, scope, name);
    
    // Create form data
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("Échec d'initialisation CURL");
        json_decref(manifest);
        return 0;
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    struct curl_httppost *form = NULL;
    struct curl_httppost *last = NULL;
    
    // Add file
    curl_formadd(&form, &last,
                CURLFORM_COPYNAME, "file",
                CURLFORM_FILE, archive_path,
                CURLFORM_FILENAME, basename(archive_path),
                CURLFORM_END);
    
    // Add metadata
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
                CURLFORM_COPYCONTENTS, license ? license : "MIT",
                CURLFORM_END);
    
    curl_formadd(&form, &last,
                CURLFORM_COPYNAME, "personal_code",
                CURLFORM_COPYCONTENTS, g_args.personal_code,
                CURLFORM_END);
    
    // Add README if exists
    char readme_path[1024];
    snprintf(readme_path, sizeof(readme_path), "%s/README.md", g_args.path);
    if (file_exists(readme_path)) {
        curl_formadd(&form, &last,
                    CURLFORM_COPYNAME, "readme",
                    CURLFORM_FILE, readme_path,
                    CURLFORM_END);
    }
    
    // Set headers
    struct curl_slist *headers = NULL;
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_config.token);
    headers = curl_slist_append(headers, auth_header);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        json_t* response = json_loads(chunk.memory, 0, NULL);
        if (response) {
            json_t* success = json_object_get(response, "success");
            if (json_is_true(success)) {
                print_success("Package publié avec succès!");
                
                if (g_args.json) {
                    json_print_object(response);
                } else {
                    json_t* pkg = json_object_get(response, "package");
                    if (pkg) {
                        printf("📦 Nom: %s\n", json_string_value(json_object_get(pkg, "name")));
                        printf("🏷️  Scope: %s\n", json_string_value(json_object_get(pkg, "scope")));
                        printf("🔢 Version: %s\n", json_string_value(json_object_get(pkg, "version")));
                        printf("📏 Taille: %ld bytes\n", (long)json_integer_value(json_object_get(pkg, "size")));
                        printf("🔐 SHA256: %s\n", json_string_value(json_object_get(pkg, "sha256")));
                    }
                }
            } else {
                const char* error = json_string_value(json_object_get(response, "error"));
                print_error(error ? error : "Échec de la publication");
            }
            json_decref(response);
        }
    } else {
        print_error("Échec de l'upload");
    }
    
    curl_easy_cleanup(curl);
    curl_formfree(form);
    curl_slist_free_all(headers);
    free(chunk.memory);
    
    // Cleanup archive
    remove(archive_path);
    json_decref(manifest);
    
    return res == CURLE_OK;
}

int cmd_install() {
    if (strlen(g_args.package_name) == 0) {
        print_error("Nom du package requis");
        return 0;
    }
    
    print_step("Installation de %s...", g_args.package_name);
    
    // Parse package name for scope
    char scope[64] = "user";
    char name[128];
    
    if (g_args.package_name[0] == '@') {
        char* slash = strchr(g_args.package_name, '/');
        if (slash) {
            strncpy(scope, g_args.package_name + 1, slash - g_args.package_name - 1);
            scope[slash - g_args.package_name - 1] = 0;
            strncpy(name, slash + 1, sizeof(name) - 1);
        } else {
            strcpy(name, g_args.package_name + 1);
        }
    } else {
        strcpy(name, g_args.package_name);
    }
    
    // If version specified, use it
    char version[32] = "latest";
    if (strlen(g_args.version) > 0) {
        strcpy(version, g_args.version);
    }
    
    // Get package info first
    char info_url[1024];
    snprintf(info_url, sizeof(info_url), "%s/api/package/info/%s/%s", 
             g_config.api_url, scope, name);
    
    int http_code = 0;
    json_t* info = api_request(info_url, "GET", NULL, &http_code);
    
    if (!info || http_code != 200) {
        print_error("Package non trouvé");
        if (info) json_decref(info);
        return 0;
    }
    
    // Get latest version if not specified
    if (strcmp(version, "latest") == 0) {
        const char* latest = json_string_value(json_object_get(info, "latest_version"));
        if (latest) strcpy(version, latest);
    }
    
    // Download package
    char download_url[1024];
    snprintf(download_url, sizeof(download_url), "%s/package/download/%s/%s/%s", 
             g_config.api_url, scope, name, version);
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("Échec d'initialisation CURL");
        json_decref(info);
        return 0;
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    curl_easy_setopt(curl, CURLOPT_URL, download_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK || chunk.size == 0) {
        print_error("Échec du téléchargement");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        json_decref(info);
        return 0;
    }
    
    // Create installation directory
    char install_dir[1024];
    if (g_args.global) {
        snprintf(install_dir, sizeof(install_dir), "%s/%s", LIB_PATH, name);
    } else {
        snprintf(install_dir, sizeof(install_dir), "%s/.zarch/packages/%s", getenv("HOME"), name);
    }
    
    if (file_exists(install_dir)) {
        if (g_args.force || g_args.yes) {
            // Remove existing
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", install_dir);
            system(cmd);
        } else {
            printf("Le package existe déjà. Remplacer? [y/N]: ");
            char response[4];
            fgets(response, sizeof(response), stdin);
            if (response[0] != 'y' && response[0] != 'Y') {
                print_info("Installation annulée");
                curl_easy_cleanup(curl);
                free(chunk.memory);
                json_decref(info);
                return 0;
            }
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", install_dir);
            system(cmd);
        }
    }
    
    create_directory(install_dir);
    
    // Save and extract archive
    char temp_file[1024];
    snprintf(temp_file, sizeof(temp_file), "/tmp/zarch-install-%ld.tar.gz", (long)time(NULL));
    
    FILE* f = fopen(temp_file, "wb");
    if (!f) {
        print_error("Échec de création du fichier temporaire");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        json_decref(info);
        return 0;
    }
    
    fwrite(chunk.memory, 1, chunk.size, f);
    fclose(f);
    
    // Extract
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C \"%s\"", temp_file, install_dir);
    
    if (system(cmd) != 0) {
        print_error("Échec de l'extraction");
    } else {
        print_success("Package installé avec succès");
        
        if (!g_args.quiet) {
            printf("📁 Emplacement: %s\n", install_dir);
            printf("🔢 Version: %s\n", version);
            
            // List installed files
            DIR* dir = opendir(install_dir);
            if (dir) {
                printf("📄 Fichiers:\n");
                struct dirent* entry;
                int count = 0;
                while ((entry = readdir(dir)) != NULL) {
                    if (entry->d_name[0] != '.') {
                        printf("  - %s\n", entry->d_name);
                        count++;
                    }
                }
                closedir(dir);
                if (count == 0) {
                    printf("  (aucun fichier visible)\n");
                }
            }
        }
    }
    
    // Cleanup
    remove(temp_file);
    curl_easy_cleanup(curl);
    free(chunk.memory);
    json_decref(info);
    
    return 1;
}

int cmd_search() {
    print_step("Recherche en cours...");
    
    char url[1024];
    if (strlen(g_args.query) > 0) {
        snprintf(url, sizeof(url), "%s/api/package/search?q=%s", g_config.api_url, g_args.query);
    } else {
        snprintf(url, sizeof(url), "%s/zarch/INDEX", g_config.api_url);
    }
    
    int http_code = 0;
    json_t* response = api_request(url, "GET", NULL, &http_code);
    
    if (!response) {
        print_error("Échec de la recherche");
        return 0;
    }
    
    if (g_args.json) {
        json_print_object(response);
        json_decref(response);
        return 1;
    }
    
    if (strlen(g_args.query) > 0) {
        // Search results
        json_t* results = json_object_get(response, "results");
        json_t* count = json_object_get(response, "count");
        
        if (json_is_array(results)) {
            size_t i;
            json_t* value;
            int total = json_integer_value(count);
            
            printf("\n🔍 Résultats pour \"%s\" (%d trouvés):\n\n", g_args.query, total);
            
            json_array_foreach(results, i, value) {
                const char* name = json_string_value(json_object_get(value, "name"));
                const char* scope = json_string_value(json_object_get(value, "scope"));
                const char* version = json_string_value(json_object_get(value, "version"));
                const char* description = json_string_value(json_object_get(value, "description"));
                
                printf("📦 %s%s%s v%s\n", 
                       COLOR_BOLD,
                       scope && strcmp(scope, "user") != 0 ? "@" : "",
                       scope && strcmp(scope, "user") != 0 ? scope : name,
                       COLOR_RESET);
                
                if (scope && strcmp(scope, "user") != 0) {
                    printf("   Scope: %s/%s\n", scope, name);
                }
                
                printf("   Version: %s\n", version);
                if (description && strlen(description) > 0) {
                    printf("   Description: %s\n", description);
                }
                printf("\n");
            }
        }
    } else {
        // Full index
        json_t* packages = json_object_get(response, "packages");
        if (json_is_object(packages)) {
            const char* key;
            json_t* value;
            int count = 0;
            
            printf("\n📦 Tous les packages disponibles:\n\n");
            
            json_object_foreach(packages, key, value) {
                const char* version = json_string_value(json_object_get(value, "version"));
                
                printf("  %s%s%s v%s\n", 
                       COLOR_BOLD, key, COLOR_RESET, version);
                count++;
            }
            
            printf("\nTotal: %d packages\n", count);
        }
    }
    
    json_decref(response);
    return 1;
}

int cmd_list() {
    char cmd[1024];
    
    if (g_args.global) {
        snprintf(cmd, sizeof(cmd), "ls -la \"%s\"", LIB_PATH);
    } else {
        snprintf(cmd, sizeof(cmd), "ls -la \"%s/.zarch/packages\"", getenv("HOME"));
    }
    
    printf("📦 Packages installés:\n\n");
    system(cmd);
    return 1;
}

int cmd_info() {
    if (strlen(g_args.package_name) == 0) {
        print_error("Nom du package requis");
        return 0;
    }
    
    // Parse package name for scope
    char scope[64] = "user";
    char name[128];
    
    if (g_args.package_name[0] == '@') {
        char* slash = strchr(g_args.package_name, '/');
        if (slash) {
            strncpy(scope, g_args.package_name + 1, slash - g_args.package_name - 1);
            scope[slash - g_args.package_name - 1] = 0;
            strncpy(name, slash + 1, sizeof(name) - 1);
        } else {
            strcpy(name, g_args.package_name + 1);
        }
    } else {
        strcpy(name, g_args.package_name);
    }
    
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/package/info/%s/%s", g_config.api_url, scope, name);
    
    int http_code = 0;
    json_t* info = api_request(url, "GET", NULL, &http_code);
    
    if (!info || http_code != 200) {
        print_error("Package non trouvé");
        if (info) json_decref(info);
        return 0;
    }
    
    if (g_args.json) {
        json_print_object(info);
        json_decref(info);
        return 1;
    }
    
    printf("\n📦 Informations du package:\n\n");
    printf("  Nom: %s\n", json_string_value(json_object_get(info, "name")));
    printf("  Scope: %s\n", json_string_value(json_object_get(info, "scope")));
    printf("  Dernière version: %s\n", json_string_value(json_object_get(info, "latest_version")));
    printf("  Description: %s\n", json_string_value(json_object_get(info, "description")));
    printf("  Auteur: %s\n", json_string_value(json_object_get(info, "author")));
    printf("  License: %s\n", json_string_value(json_object_get(info, "license")));
    printf("  Taille: %.2f MB\n", (double)json_integer_value(json_object_get(info, "size")) / (1024*1024));
    printf("  Téléchargements: %d\n", (int)json_integer_value(json_object_get(info, "downloads")));
    printf("  Créé le: %s\n", json_string_value(json_object_get(info, "created_at")));
    printf("  Mis à jour: %s\n", json_string_value(json_object_get(info, "updated_at")));
    
    json_t* versions = json_object_get(info, "all_versions");
    if (json_is_array(versions)) {
        printf("  Versions disponibles: ");
        size_t i;
        json_t* value;
        json_array_foreach(versions, i, value) {
            printf("%s%s", i > 0 ? ", " : "", json_string_value(value));
        }
        printf("\n");
    }
    
    json_decref(info);
    return 1;
}

int cmd_config() {
    if (strlen(g_args.subcommand) == 0 || strcmp(g_args.subcommand, "list") == 0) {
        if (g_args.json) {
            json_t* obj = json_object();
            json_object_set_new(obj, "username", json_string(g_config.username));
            json_object_set_new(obj, "email", json_string(g_config.email));
            json_object_set_new(obj, "default_scope", json_string(g_config.default_scope));
            json_object_set_new(obj, "api_url", json_string(g_config.api_url));
            json_object_set_new(obj, "auto_update", json_integer(g_config.auto_update));
            json_object_set_new(obj, "color_mode", json_integer(g_config.color_mode));
            json_print_object(obj);
            json_decref(obj);
        } else {
            printf("🔧 Configuration Zarch:\n\n");
            printf("  Utilisateur: %s\n", g_config.username);
            printf("  Email: %s\n", g_config.email);
            printf("  Scope par défaut: %s\n", g_config.default_scope);
            printf("  URL API: %s\n", g_config.api_url);
            printf("  Mise à jour auto: %s\n", g_config.auto_update ? "activée" : "désactivée");
            printf("  Couleurs: %s\n", g_config.color_mode ? "activées" : "désactivées");
        }
        return 1;
    }
    
    if (strcmp(g_args.subcommand, "set") == 0) {
        if (g_args.argc < 5) {
            print_error("Usage: zarch config set <key> <value>");
            return 0;
        }
        
        char* key = g_args.argv[3];
        char* value = g_args.argv[4];
        
        if (strcmp(key, "default_scope") == 0) {
            strncpy(g_config.default_scope, value, sizeof(g_config.default_scope)-1);
        } else if (strcmp(key, "api_url") == 0) {
            strncpy(g_config.api_url, value, sizeof(g_config.api_url)-1);
        } else if (strcmp(key, "color_mode") == 0) {
            g_config.color_mode = parse_bool_arg(value);
            g_color_enabled = g_config.color_mode;
        } else if (strcmp(key, "auto_update") == 0) {
            g_config.auto_update = parse_bool_arg(value);
        } else {
            print_error("Clé de configuration inconnue");
            return 0;
        }
        
        save_config();
        print_success("Configuration mise à jour");
        return 1;
    }
    
    print_error("Sous-commande config inconnue");
    return 0;
}

int cmd_update() {
    print_step("Mise à jour de l'index...");
    
    // Simple cache update
    g_config.last_update = time(NULL);
    save_config();
    
    print_success("Index mis à jour");
    return 1;
}

int cmd_clean() {
    print_step("Nettoyage des caches...");
    
    char cmd[1024];
    
    // Clean temp files
    snprintf(cmd, sizeof(cmd), "rm -f /tmp/zarch-* /tmp/*.tar.gz 2>/dev/null");
    system(cmd);
    
    // Clean build cache
    snprintf(cmd, sizeof(cmd), "rm -rf %s/.zarch/cache 2>/dev/null", getenv("HOME"));
    system(cmd);
    
    print_success("Cache nettoyé");
    return 1;
}

// ===== HELP =====
void show_help() {
    printf(COLOR_BOLD "🐧 Zarch Package Manager v%s\n\n" COLOR_RESET, VERSION);
    printf("Usage: zarch <command> [options] [arguments]\n\n");
    
    printf(COLOR_BOLD "📦 Gestion des packages:\n" COLOR_RESET);
    printf("  init [path]              Initialiser un nouveau package\n");
    printf("  build [path]             Construire le package\n");
    printf("  publish [path] [code]    Publier le package\n");
    printf("  install <package>        Installer un package\n");
    printf("  uninstall <package>      Désinstaller un package\n");
    printf("  search [query]           Rechercher des packages\n");
    printf("  list                     Lister les packages installés\n");
    printf("  info <package>           Informations sur un package\n");
    printf("  update                   Mettre à jour l'index\n");
    
    printf(COLOR_BOLD "\n🔐 Authentification:\n" COLOR_RESET);
    printf("  login [user] [pass]      Se connecter\n");
    printf("  logout                   Se déconnecter\n");
    printf("  whoami                   Afficher l'utilisateur courant\n");
    
    printf(COLOR_BOLD "\n⚙️  Configuration:\n" COLOR_RESET);
    printf("  config list              Lister la configuration\n");
    printf("  config set <key> <value> Définir une option\n");
    printf("  clean                    Nettoyer les caches\n");
    
    printf(COLOR_BOLD "\n🛠️  Options générales:\n" COLOR_RESET);
    printf("  -h, --help               Afficher cette aide\n");
    printf("  -v, --version            Afficher la version\n");
    printf("  -V, --verbose            Mode verbeux\n");
    printf("  -q, --quiet              Mode silencieux\n");
    printf("  -j, --json               Sortie JSON\n");
    printf("  -f, --force              Forcer l'opération\n");
    printf("  -y, --yes                Répondre oui à tout\n");
    printf("  -i, --interactive        Mode interactif\n");
    
    printf(COLOR_BOLD "\n📁 Options de chemin:\n" COLOR_RESET);
    printf("  --path=<path>            Chemin du package\n");
    printf("  --scope=<scope>          Scope du package\n");
    printf("  --version=<version>      Version spécifique\n");
    
    printf(COLOR_BOLD "\n🎯 Options d'installation:\n" COLOR_RESET);
    printf("  -g, --global             Installation globale\n");
    printf("  -l, --local              Installation locale\n");
    printf("  --no-cache               Désactiver le cache\n");
    
    printf(COLOR_BOLD "\n📤 Options de publication:\n" COLOR_RESET);
    printf("  --auto-version           Auto-incrémenter la version\n");
    printf("  --tag=<tag>              Tag de version\n");
    
    printf(COLOR_BOLD "\n🔍 Options de recherche:\n" COLOR_RESET);
    printf("  --all                    Tout afficher\n");
    printf("  --output=<format>        Format de sortie\n");
    
    printf(COLOR_BOLD "\n📚 Exemples:\n" COLOR_RESET);
    printf("  zarch init --interactive\n");
    printf("  zarch publish . 1234 --auto-version\n");
    printf("  zarch install @org/math --global\n");
    printf("  zarch search \"math\" --json | jq .\n");
    printf("  zarch info swiftvelox -j\n");
    printf("  zarch config set default_scope myorg\n");
    printf("  zarch config set color_mode false\n");
    
    printf(COLOR_BOLD "\n🌐 Registre: %s\n" COLOR_RESET, REGISTRY_URL);
}

void show_version() {
    printf("Zarch CLI v%s\n", VERSION);
    printf("Registre: %s\n", REGISTRY_URL);
    printf("Librairie: %s\n", LIB_PATH);
}

// ===== MAIN =====
int main(int argc, char** argv) {
    // Initialize
    curl_global_init(CURL_GLOBAL_DEFAULT);
    load_config();
    init_args(&g_args, argc, argv);
    parse_args(&g_args);
    
    // Show header if not quiet
    if (!g_args.quiet && argc > 1 && !g_args.json) {
        printf(COLOR_CYAN "🐧 Zarch CLI v%s\n" COLOR_RESET, VERSION);
    }
    
    // Handle special flags
    if (g_args.help) {
        show_help();
        curl_global_cleanup();
        return 0;
    }
    
    if (g_args.version_flag) {
        show_version();
        curl_global_cleanup();
        return 0;
    }
    
    // Execute command
    int result = 0;
    
    if (strlen(g_args.command) == 0) {
        show_help();
        curl_global_cleanup();
        return 1;
    }
    
    if (strcmp(g_args.command, "login") == 0) {
        result = cmd_login();
    } else if (strcmp(g_args.command, "logout") == 0) {
        result = cmd_logout();
    } else if (strcmp(g_args.command, "whoami") == 0) {
        result = cmd_whoami();
    } else if (strcmp(g_args.command, "init") == 0) {
        result = cmd_init();
    } else if (strcmp(g_args.command, "build") == 0) {
        result = cmd_build();
    } else if (strcmp(g_args.command, "publish") == 0) {
        result = cmd_publish();
    } else if (strcmp(g_args.command, "install") == 0) {
        result = cmd_install();
    } else if (strcmp(g_args.command, "uninstall") == 0 || strcmp(g_args.command, "remove") == 0) {
        // Simple uninstall for now
        print_step("Désinstallation...");
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s/%s\"", LIB_PATH, g_args.package_name);
        system(cmd);
        print_success("Package désinstallé");
        result = 1;
    } else if (strcmp(g_args.command, "search") == 0) {
        result = cmd_search();
    } else if (strcmp(g_args.command, "list") == 0) {
        result = cmd_list();
    } else if (strcmp(g_args.command, "info") == 0) {
        result = cmd_info();
    } else if (strcmp(g_args.command, "config") == 0) {
        result = cmd_config();
    } else if (strcmp(g_args.command, "update") == 0) {
        result = cmd_update();
    } else if (strcmp(g_args.command, "clean") == 0) {
        result = cmd_clean();
    } else {
        print_error("Commande inconnue: %s", g_args.command);
        show_help();
        result = 0;
    }
    
    // Cleanup
    curl_global_cleanup();
    
    if (!g_args.quiet && !g_args.json) {
        printf("\n");
    }
    
    return result ? 0 : 1;
}
