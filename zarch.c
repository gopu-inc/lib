#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <jansson.h>
#include <fnmatch.h>
#include <errno.h>
#include <libgen.h>
#include <ctype.h>

// ============================================================================
// CONFIGURATION
// ============================================================================
#define HUB_URL "https://zenv-hub.onrender.com"
#define MAX_PATH 4096
#define MAX_LINE 1024
#define MAX_NAME 256
#define CONFIG_FILE "/etc/zarch/config.json"
#define TOKEN_FILE "/etc/zarch/token"
#define CACHE_DIR "/var/cache/zarch"
#define INSTALL_DIR "/usr/local/zenv"
#define VERSION "3.0.0"

// ============================================================================
// STRUCTURES
// ============================================================================
typedef struct {
    char name[100];
    char version[50];
    char description[500];
    char author[100];
    char license[50];
    char build_dir[MAX_PATH];
    char output_file[MAX_PATH];
    char **includes;
    int include_count;
    char **excludes;
    int exclude_count;
    char dependencies[10][100];
    int dep_count;
    char scripts[10][200];
    int script_count;
} PackageConfig;

typedef struct {
    char *data;
    size_t size;
} MemoryStruct;

typedef struct {
    char token[256];
    char username[100];
    char role[50];
    time_t expires;
} UserSession;

typedef struct {
    char name[100];
    char version[50];
    char description[500];
    char author[100];
    char license[50];
    char download_url[MAX_PATH];
    char readme_url[MAX_PATH];
    char license_url[MAX_PATH];
    char docs_url[MAX_PATH];
    long size;
    int downloads;
    char updated_at[50];
} PackageInfo;

typedef struct {
    PackageInfo *packages;
    int count;
} PackageList;

// ============================================================================
// UTILITAIRES FICHIERS AMÉLIORÉS
// ============================================================================
int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int is_directory(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

void create_directory(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

int copy_file(const char *src, const char *dst) {
    FILE *src_fp = fopen(src, "rb");
    if (!src_fp) return 0;
    
    FILE *dst_fp = fopen(dst, "wb");
    if (!dst_fp) {
        fclose(src_fp);
        return 0;
    }
    
    char buffer[8192];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), src_fp)) > 0) {
        fwrite(buffer, 1, bytes, dst_fp);
    }
    
    fclose(src_fp);
    fclose(dst_fp);
    
    // Copier les permissions
    struct stat st;
    if (stat(src, &st) == 0) {
        chmod(dst, st.st_mode & 0777);
    }
    
    return 1;
}

int remove_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return 0;
    
    struct dirent *entry;
    char full_path[MAX_PATH];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        if (is_directory(full_path)) {
            remove_directory(full_path);
        } else {
            remove(full_path);
        }
    }
    
    closedir(dir);
    return rmdir(path);
}

// ============================================================================
// UTILITAIRES CHEMINS
// ============================================================================
char* get_real_path(const char *path) {
    static char real[MAX_PATH];
    if (realpath(path, real) == NULL) {
        strncpy(real, path, MAX_PATH - 1);
    }
    return real;
}

char* get_relative_path(const char *base, const char *path) {
    static char relative[MAX_PATH];
    const char *p = strstr(path, base);
    
    if (p == path) {
        p += strlen(base);
        if (*p == '/') p++;
        strncpy(relative, p, MAX_PATH - 1);
    } else {
        strncpy(relative, path, MAX_PATH - 1);
    }
    
    return relative;
}

int matches_pattern(const char *path, const char *pattern) {
    return fnmatch(pattern, path, FNM_PATHNAME | FNM_PERIOD) == 0;
}

// ============================================================================
// JSON UTILITIES AMÉLIORÉES
// ============================================================================
int parse_json_config(const char *filename, PackageConfig *config) {
    json_t *root;
    json_error_t error;
    
    root = json_load_file(filename, 0, &error);
    if (!root) {
        printf("❌ JSON error: %s (line %d, column %d)\n", 
               error.text, error.line, error.column);
        return 0;
    }
    
    // Initialiser la structure
    memset(config, 0, sizeof(PackageConfig));
    config->includes = NULL;
    config->excludes = NULL;
    config->dep_count = 0;
    config->script_count = 0;
    
    // Parser les champs
    json_t *name = json_object_get(root, "name");
    json_t *version = json_object_get(root, "version");
    json_t *desc = json_object_get(root, "description");
    json_t *author = json_object_get(root, "author");
    json_t *license = json_object_get(root, "license");
    json_t *build_dir = json_object_get(root, "build_dir");
    json_t *output = json_object_get(root, "output");
    
    if (name) strncpy(config->name, json_string_value(name), sizeof(config->name)-1);
    if (version) strncpy(config->version, json_string_value(version), sizeof(config->version)-1);
    if (desc) strncpy(config->description, json_string_value(desc), sizeof(config->description)-1);
    if (author) strncpy(config->author, json_string_value(author), sizeof(config->author)-1);
    if (license) strncpy(config->license, json_string_value(license), sizeof(config->license)-1);
    if (build_dir) strncpy(config->build_dir, json_string_value(build_dir), sizeof(config->build_dir)-1);
    if (output) strncpy(config->output_file, json_string_value(output), sizeof(config->output_file)-1);
    
    // Valeurs par défaut
    if (strlen(config->build_dir) == 0) {
        char cwd[MAX_PATH];
        getcwd(cwd, sizeof(cwd));
        strcpy(config->build_dir, cwd);
    }
    
    if (strlen(config->license) == 0) strcpy(config->license, "MIT");
    if (strlen(config->author) == 0) strcpy(config->author, "unknown");
    if (strlen(config->output_file) == 0) {
        snprintf(config->output_file, sizeof(config->output_file), 
                "%s-%s.zv", config->name, config->version);
    }
    
    // Parser les includes
    json_t *includes = json_object_get(root, "include");
    if (includes) {
        if (json_is_array(includes)) {
            size_t index;
            json_t *value;
            
            json_array_foreach(includes, index, value) {
                if (json_is_string(value)) {
                    config->includes = realloc(config->includes, (config->include_count + 1) * sizeof(char*));
                    config->includes[config->include_count] = strdup(json_string_value(value));
                    config->include_count++;
                }
            }
        } else if (json_is_string(includes)) {
            config->includes = malloc(sizeof(char*));
            config->includes[0] = strdup(json_string_value(includes));
            config->include_count = 1;
        }
    }
    
    // Parser les excludes
    json_t *excludes = json_object_get(root, "exclude");
    if (excludes) {
        if (json_is_array(excludes)) {
            size_t index;
            json_t *value;
            
            json_array_foreach(excludes, index, value) {
                if (json_is_string(value)) {
                    config->excludes = realloc(config->excludes, (config->exclude_count + 1) * sizeof(char*));
                    config->excludes[config->exclude_count] = strdup(json_string_value(value));
                    config->exclude_count++;
                }
            }
        } else if (json_is_string(excludes)) {
            config->excludes = malloc(sizeof(char*));
            config->excludes[0] = strdup(json_string_value(excludes));
            config->exclude_count = 1;
        }
    }
    
    // Parser les dépendances
    json_t *deps = json_object_get(root, "dependencies");
    if (deps && json_is_array(deps)) {
        size_t index;
        json_t *value;
        
        json_array_foreach(deps, index, value) {
            if (json_is_string(value) && config->dep_count < 10) {
                strncpy(config->dependencies[config->dep_count], 
                       json_string_value(value), 
                       sizeof(config->dependencies[0])-1);
                config->dep_count++;
            }
        }
    }
    
    // Parser les scripts
    json_t *scripts = json_object_get(root, "scripts");
    if (scripts && json_is_object(scripts)) {
        const char *key;
        json_t *value;
        
        json_object_foreach(scripts, key, value) {
            if (json_is_string(value) && config->script_count < 10) {
                snprintf(config->scripts[config->script_count], 
                        sizeof(config->scripts[0]),
                        "%s:%s", key, json_string_value(value));
                config->script_count++;
            }
        }
    }
    
    json_decref(root);
    
    // Validation
    if (strlen(config->name) == 0) {
        printf("❌ Package 'name' is required\n");
        return 0;
    }
    
    // Valider le nom du package
    for (int i = 0; config->name[i]; i++) {
        if (!isalnum(config->name[i]) && config->name[i] != '-' && config->name[i] != '_') {
            printf("❌ Invalid package name. Use only alphanumeric, dash and underscore\n");
            return 0;
        }
    }
    
    if (strlen(config->version) == 0) {
        strcpy(config->version, "1.0.0");
    }
    
    return 1;
}

void free_package_config(PackageConfig *config) {
    for (int i = 0; i < config->include_count; i++) {
        free(config->includes[i]);
    }
    free(config->includes);
    
    for (int i = 0; i < config->exclude_count; i++) {
        free(config->excludes[i]);
    }
    free(config->excludes);
}

void create_lock_file(const PackageConfig *config, const char *output_file) {
    char lock_file[MAX_PATH];
    snprintf(lock_file, sizeof(lock_file), "%s-lock.json", config->name);
    
    json_t *root = json_object();
    json_object_set_new(root, "name", json_string(config->name));
    json_object_set_new(root, "version", json_string(config->version));
    json_object_set_new(root, "author", json_string(config->author));
    json_object_set_new(root, "license", json_string(config->license));
    json_object_set_new(root, "description", json_string(config->description));
    json_object_set_new(root, "output_file", json_string(output_file));
    json_object_set_new(root, "build_date", json_integer(time(NULL)));
    
    // Fichiers inclus
    json_t *files = json_array();
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "tar -tzf %s 2>/dev/null", output_file);
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[MAX_PATH];
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = 0;
            if (strlen(line) > 0) {
                json_array_append_new(files, json_string(line));
            }
        }
        pclose(fp);
    }
    json_object_set_new(root, "files", files);
    
    // Hash du fichier
    snprintf(cmd, sizeof(cmd), "sha256sum %s | cut -d' ' -f1", output_file);
    fp = popen(cmd, "r");
    if (fp) {
        char hash[65];
        if (fgets(hash, sizeof(hash), fp)) {
            hash[strcspn(hash, "\n")] = 0;
            json_object_set_new(root, "sha256", json_string(hash));
        }
        pclose(fp);
    }
    
    // Dépendances
    if (config->dep_count > 0) {
        json_t *deps = json_array();
        for (int i = 0; i < config->dep_count; i++) {
            json_array_append_new(deps, json_string(config->dependencies[i]));
        }
        json_object_set_new(root, "dependencies", deps);
    }
    
    // Écrire le fichier
    json_dump_file(root, lock_file, JSON_INDENT(2));
    json_decref(root);
    
    printf("🔒 Lock file created: %s\n", lock_file);
}

// ============================================================================
// EXTRACT METADATA FROM ARCHIVE
// ============================================================================
int extract_metadata_from_archive(const char *archive_path, PackageConfig *config) {
    // Créer un répertoire temporaire
    char temp_dir[MAX_PATH];
    snprintf(temp_dir, sizeof(temp_dir), "/tmp/zarch-extract-%d", getpid());
    create_directory(temp_dir);
    
    // Extraire uniquement le fichier manifeste
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "tar -xzf %s .zarch-manifest -C %s 2>/dev/null", 
             archive_path, temp_dir);
    
    int result = system(cmd);
    if (result != 0) {
        // Essayer de lire directement depuis l'archive avec tar -t
        snprintf(cmd, sizeof(cmd), "tar -tzf %s 2>/dev/null | head -20", archive_path);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char line[MAX_PATH];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, ".zarch-manifest")) {
                    // Extraire le manifeste
                    snprintf(cmd, sizeof(cmd), "tar -xzf %s .zarch-manifest -C %s 2>/dev/null", 
                            archive_path, temp_dir);
                    system(cmd);
                    break;
                }
            }
            pclose(fp);
        }
    }
    
    // Lire le fichier manifeste
    char manifest_path[MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/.zarch-manifest", temp_dir);
    
    if (file_exists(manifest_path)) {
        FILE *fp = fopen(manifest_path, "r");
        if (fp) {
            char line[MAX_LINE];
            
            while (fgets(line, sizeof(line), fp)) {
                line[strcspn(line, "\n")] = 0;
                
                if (strstr(line, "Package:") == line) {
                    strncpy(config->name, line + 9, sizeof(config->name)-1);
                } else if (strstr(line, "Version:") == line) {
                    strncpy(config->version, line + 9, sizeof(config->version)-1);
                } else if (strstr(line, "Author:") == line) {
                    strncpy(config->author, line + 8, sizeof(config->author)-1);
                } else if (strstr(line, "License:") == line) {
                    strncpy(config->license, line + 9, sizeof(config->license)-1);
                } else if (strstr(line, "Description:") == line) {
                    strncpy(config->description, line + 13, sizeof(config->description)-1);
                }
            }
            fclose(fp);
        }
    } else {
        // Si pas de manifeste, essayer d'extraire le nom du fichier
        char *base = strdup(archive_path);
        char *filename = basename(base);
        char *dot = strrchr(filename, '.');
        if (dot) *dot = '\0';
        
        // Essayer de parser name-version.zv
        char *dash = strrchr(filename, '-');
        if (dash) {
            *dash = '\0';
            strncpy(config->name, filename, sizeof(config->name)-1);
            strncpy(config->version, dash + 1, sizeof(config->version)-1);
        } else {
            strncpy(config->name, filename, sizeof(config->name)-1);
            strcpy(config->version, "1.0.0");
        }
        
        free(base);
    }
    
    // Nettoyer
    remove_directory(temp_dir);
    
    // Valeurs par défaut si manquantes
    if (strlen(config->name) == 0) {
        strcpy(config->name, "unknown");
    }
    if (strlen(config->version) == 0) {
        strcpy(config->version, "1.0.0");
    }
    if (strlen(config->author) == 0) {
        strcpy(config->author, "unknown");
    }
    if (strlen(config->license) == 0) {
        strcpy(config->license, "MIT");
    }
    
    return 1;
}

// ============================================================================
// CURL UTILITIES AMÉLIORÉES
// ============================================================================
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;
    
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

static size_t write_file_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    FILE *fp = (FILE *)userp;
    return fwrite(contents, size, nmemb, fp);
}

char* http_get(const char *url, const char *token) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    struct curl_slist *headers = NULL;
    if (token && strlen(token) > 0) {
        char auth_header[300];
        snprintf(auth_header, sizeof(auth_header), "Authorization: %s", token);
        headers = curl_slist_append(headers, auth_header);
    }
    
    MemoryStruct chunk = {NULL, 0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(chunk.data);
        return NULL;
    }
    
    return chunk.data;
}

char* http_post_json(const char *url, const char *token, const char *json_data) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (token && strlen(token) > 0) {
        char auth_header[300];
        snprintf(auth_header, sizeof(auth_header), "Authorization: %s", token);
        headers = curl_slist_append(headers, auth_header);
    }
    
    MemoryStruct chunk = {NULL, 0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(chunk.data);
        return NULL;
    }
    
    return chunk.data;
}

int http_download_file(const char *url, const char *output_path, const char *token) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;
    
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return 0;
    }
    
    struct curl_slist *headers = NULL;
    if (token && strlen(token) > 0) {
        char auth_header[300];
        snprintf(auth_header, sizeof(auth_header), "Authorization: %s", token);
        headers = curl_slist_append(headers, auth_header);
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    
    fclose(fp);
    
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return res == CURLE_OK;
}

// ============================================================================
// SESSION MANAGEMENT
// ============================================================================
UserSession* load_session() {
    static UserSession session = {"", "", "", 0};
    
    if (file_exists(TOKEN_FILE)) {
        json_t *root = json_load_file(TOKEN_FILE, 0, NULL);
        if (root) {
            json_t *token = json_object_get(root, "token");
            json_t *username = json_object_get(root, "username");
            json_t *role = json_object_get(root, "role");
            json_t *expires = json_object_get(root, "expires");
            
            if (token) strncpy(session.token, json_string_value(token), sizeof(session.token)-1);
            if (username) strncpy(session.username, json_string_value(username), sizeof(session.username)-1);
            if (role) strncpy(session.role, json_string_value(role), sizeof(session.role)-1);
            if (expires) session.expires = json_integer_value(expires);
            
            json_decref(root);
            
            // Vérifier expiration
            if (session.expires > 0 && time(NULL) > session.expires) {
                printf("⚠️  Token expired\n");
                memset(&session, 0, sizeof(session));
                remove(TOKEN_FILE);
            }
        }
    }
    
    return &session;
}

void save_session(const UserSession *session) {
    create_directory("/etc/zarch");
    
    json_t *root = json_object();
    json_object_set_new(root, "token", json_string(session->token));
    json_object_set_new(root, "username", json_string(session->username));
    json_object_set_new(root, "role", json_string(session->role));
    json_object_set_new(root, "expires", json_integer(session->expires));
    
    json_dump_file(root, TOKEN_FILE, JSON_INDENT(2));
    json_decref(root);
    
    chmod(TOKEN_FILE, 0600);
}

void clear_session() {
    remove(TOKEN_FILE);
    printf("✅ Logged out\n");
}

// ============================================================================
// BUILD SYSTEM AMÉLIORÉ
// ============================================================================
int should_include_file(const char *filepath, const PackageConfig *config) {
    char relative_path[MAX_PATH];
    
    // Obtenir le chemin réel
    char *real_path = get_real_path(filepath);
    
    // Convertir en chemin relatif
    if (strstr(real_path, config->build_dir) == real_path) {
        const char *p = real_path + strlen(config->build_dir);
        if (*p == '/') p++;
        strncpy(relative_path, p, sizeof(relative_path)-1);
    } else {
        // Si pas dans build_dir, utiliser le chemin complet
        strncpy(relative_path, real_path, sizeof(relative_path)-1);
    }
    
    // Exclusions par défaut
    const char *default_excludes[] = {
        ".git/", ".svn/", ".hg/", ".zarch/", "__pycache__/", 
        "node_modules/", ".DS_Store", "*.tmp", "*.swp", "*.swo",
        "*.log", "*.bak", "*.backup", "*.pid"
    };
    
    for (int i = 0; i < sizeof(default_excludes)/sizeof(default_excludes[0]); i++) {
        if (matches_pattern(relative_path, default_excludes[i])) {
            return 0;
        }
    }
    
    // Vérifier les exclusions configurées
    for (int i = 0; i < config->exclude_count; i++) {
        if (matches_pattern(relative_path, config->excludes[i])) {
            return 0;
        }
    }
    
    // Si des inclusions sont spécifiées, vérifier
    if (config->include_count > 0) {
        for (int i = 0; i < config->include_count; i++) {
            if (matches_pattern(relative_path, config->includes[i])) {
                return 1;
            }
        }
        return 0;
    }
    
    return 1;
}

void collect_files_recursive(const char *base_dir, const char *dir_path, 
                           PackageConfig *config, char ***files, int *file_count,
                           char ***directories, int *dir_count) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    
    struct dirent *entry;
    char path[MAX_PATH];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        
        if (is_directory(path)) {
            // Ajouter le répertoire à la liste
            *directories = realloc(*directories, (*dir_count + 1) * sizeof(char *));
            (*directories)[*dir_count] = strdup(get_relative_path(base_dir, path));
            (*dir_count)++;
            
            collect_files_recursive(base_dir, path, config, files, file_count, directories, dir_count);
        } else {
            if (should_include_file(path, config)) {
                *files = realloc(*files, (*file_count + 1) * sizeof(char *));
                (*files)[*file_count] = strdup(get_relative_path(base_dir, path));
                (*file_count)++;
            }
        }
    }
    
    closedir(dir);
}

int create_package_archive(const PackageConfig *config) {
    printf("📦 Building package: %s v%s\n", config->name, config->version);
    printf("📁 Source directory: %s\n", config->build_dir);
    printf("📄 Output file: %s\n", config->output_file);
    
    // Vérifier le répertoire source
    if (!is_directory(config->build_dir)) {
        printf("❌ Build directory doesn't exist: %s\n", config->build_dir);
        return 0;
    }
    
    // Collecter les fichiers
    char **files = NULL;
    char **directories = NULL;
    int file_count = 0;
    int dir_count = 0;
    
    collect_files_recursive(config->build_dir, config->build_dir, config, 
                          &files, &file_count, &directories, &dir_count);
    
    if (file_count == 0 && dir_count == 0) {
        printf("❌ No files to package\n");
        return 0;
    }
    
    printf("📊 Files to include: %d\n", file_count);
    printf("📁 Directories to include: %d\n", dir_count);
    
    // Créer le fichier manifeste
    char manifest_path[MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/.zarch-manifest", config->build_dir);
    
    FILE *manifest = fopen(manifest_path, "w");
    if (manifest) {
        fprintf(manifest, "Package: %s\n", config->name);
        fprintf(manifest, "Version: %s\n", config->version);
        fprintf(manifest, "Author: %s\n", config->author);
        fprintf(manifest, "License: %s\n", config->license);
        fprintf(manifest, "Description: %s\n", config->description);
        fprintf(manifest, "BuildDate: %ld\n", time(NULL));
        fprintf(manifest, "Files: %d\n", file_count);
        fprintf(manifest, "Directories: %d\n", dir_count);
        
        if (config->dep_count > 0) {
            fprintf(manifest, "\nDependencies:\n");
            for (int i = 0; i < config->dep_count; i++) {
                fprintf(manifest, "- %s\n", config->dependencies[i]);
            }
        }
        
        fclose(manifest);
        
        // Ajouter le manifeste à la liste des fichiers
        files = realloc(files, (file_count + 1) * sizeof(char *));
        files[file_count] = strdup(".zarch-manifest");
        file_count++;
    }
    
    // Créer la liste de fichiers pour tar
    char temp_list_file[MAX_PATH];
    snprintf(temp_list_file, sizeof(temp_list_file), "/tmp/zarch-filelist-%d.txt", getpid());
    
    FILE *list_fp = fopen(temp_list_file, "w");
    if (!list_fp) {
        printf("❌ Cannot create temporary file list\n");
        goto cleanup;
    }
    
    // Écrire les répertoires d'abord (pour assurer leur création)
    for (int i = 0; i < dir_count; i++) {
        fprintf(list_fp, "%s\n", directories[i]);
        free(directories[i]);
    }
    free(directories);
    
    // Écrire les fichiers
    for (int i = 0; i < file_count; i++) {
        fprintf(list_fp, "%s\n", files[i]);
        free(files[i]);
    }
    free(files);
    
    fclose(list_fp);
    
    // Changer de répertoire
    char cwd[MAX_PATH];
    getcwd(cwd, sizeof(cwd));
    
    if (chdir(config->build_dir) != 0) {
        printf("❌ Cannot change to build directory\n");
        remove(temp_list_file);
        return 0;
    }
    
    // Créer l'archive avec tar
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "tar -czf %s --files-from=%s 2>/dev/null", 
             config->output_file, temp_list_file);
    
    printf("⚙️  Creating archive...\n");
    int result = system(cmd);
    
    // Nettoyer
    remove(temp_list_file);
    remove(manifest_path);
    
    // Revenir au répertoire original
    chdir(cwd);
    
    if (result == 0 && file_exists(config->output_file)) {
        // Obtenir la taille
        struct stat st;
        stat(config->output_file, &st);
        printf("✅ Package created: %s (%.2f MB)\n", 
               config->output_file, st.st_size / (1024.0 * 1024.0));
        
        // Créer le fichier lock
        create_lock_file(config, config->output_file);
        return 1;
    } else {
        printf("❌ Failed to create package\n");
        return 0;
    }
    
cleanup:
    for (int i = 0; i < file_count; i++) free(files[i]);
    for (int i = 0; i < dir_count; i++) free(directories[i]);
    free(files);
    free(directories);
    return 0;
}

// ============================================================================
// PACKAGE MANAGEMENT
// ============================================================================
PackageList* search_packages(const char *query) {
    static PackageList result = {NULL, 0};
    
    // Libérer la liste précédente
    if (result.packages) {
        free(result.packages);
        result.packages = NULL;
        result.count = 0;
    }
    
    char url[500];
    if (query && strlen(query) > 0) {
        snprintf(url, sizeof(url), "%s/api/packages/search?q=%s", HUB_URL, query);
    } else {
        snprintf(url, sizeof(url), "%s/api/packages", HUB_URL);
    }
    
    char *response = http_get(url, NULL);
    if (!response) {
        printf("❌ Failed to connect to hub\n");
        return NULL;
    }
    
    json_t *root = json_loads(response, 0, NULL);
    free(response);
    
    if (!root) {
        printf("❌ Invalid response from hub\n");
        return NULL;
    }
    
    json_t *packages = json_object_get(root, "packages");
    if (!packages || !json_is_array(packages)) {
        json_decref(root);
        return NULL;
    }
    
    size_t count = json_array_size(packages);
    result.packages = malloc(count * sizeof(PackageInfo));
    
    for (size_t i = 0; i < count; i++) {
        json_t *pkg = json_array_get(packages, i);
        
        json_t *name = json_object_get(pkg, "name");
        json_t *version = json_object_get(pkg, "version");
        json_t *desc = json_object_get(pkg, "description");
        json_t *author = json_object_get(pkg, "author");
        json_t *license = json_object_get(pkg, "license");
        json_t *download_url = json_object_get(pkg, "download_url");
        json_t *readme_url = json_object_get(pkg, "readme_url");
        json_t *license_url = json_object_get(pkg, "license_url");
        json_t *docs_url = json_object_get(pkg, "docs_url");
        json_t *size = json_object_get(pkg, "size");
        json_t *downloads = json_object_get(pkg, "downloads_count");
        json_t *updated = json_object_get(pkg, "updated_at");
        
        if (name) strncpy(result.packages[i].name, json_string_value(name), 
                         sizeof(result.packages[i].name)-1);
        if (version) strncpy(result.packages[i].version, json_string_value(version),
                           sizeof(result.packages[i].version)-1);
        if (desc) strncpy(result.packages[i].description, json_string_value(desc),
                         sizeof(result.packages[i].description)-1);
        if (author) strncpy(result.packages[i].author, json_string_value(author),
                          sizeof(result.packages[i].author)-1);
        if (license) strncpy(result.packages[i].license, json_string_value(license),
                           sizeof(result.packages[i].license)-1);
        if (download_url) strncpy(result.packages[i].download_url, 
                                json_string_value(download_url),
                                sizeof(result.packages[i].download_url)-1);
        if (readme_url) strncpy(result.packages[i].readme_url, 
                              json_string_value(readme_url),
                              sizeof(result.packages[i].readme_url)-1);
        if (license_url) strncpy(result.packages[i].license_url, 
                               json_string_value(license_url),
                               sizeof(result.packages[i].license_url)-1);
        if (docs_url) strncpy(result.packages[i].docs_url, 
                            json_string_value(docs_url),
                            sizeof(result.packages[i].docs_url)-1);
        if (size) result.packages[i].size = json_integer_value(size);
        if (downloads) result.packages[i].downloads = json_integer_value(downloads);
        if (updated) strncpy(result.packages[i].updated_at, json_string_value(updated),
                           sizeof(result.packages[i].updated_at)-1);
    }
    
    result.count = count;
    json_decref(root);
    
    return &result;
}

void show_package_info(const PackageInfo *pkg) {
    printf("\n📦 %s v%s\n", pkg->name, pkg->version);
    printf("📝 %s\n", pkg->description);
    printf("👤 Author: %s\n", pkg->author);
    printf("⚖️  License: %s\n", pkg->license);
    printf("📥 Downloads: %d\n", pkg->downloads);
    printf("💾 Size: %.2f MB\n", pkg->size / (1024.0 * 1024.0));
    printf("🕒 Updated: %s\n", pkg->updated_at);
    printf("🔗 Download: %s\n\n", pkg->download_url);
}

// ============================================================================
// INSTALLATION SYSTEM
// ============================================================================
int install_package(const char *package_name, const char *version, const char *target_dir) {
    printf("📦 Installing package: %s", package_name);
    if (version) printf(" v%s", version);
    printf("\n");
    
    // Construire l'URL de téléchargement
    char url[500];
    if (version) {
        snprintf(url, sizeof(url), "%s/api/packages/download/%s/%s", 
                 HUB_URL, package_name, version);
    } else {
        snprintf(url, sizeof(url), "%s/api/packages/download/%s/latest", 
                 HUB_URL, package_name);
    }
    
    // Créer le répertoire de cache
    create_directory(CACHE_DIR);
    
    // Nom du fichier temporaire
    char temp_file[MAX_PATH];
    if (version) {
        snprintf(temp_file, sizeof(temp_file), "%s/%s-%s.zv", 
                 CACHE_DIR, package_name, version);
    } else {
        snprintf(temp_file, sizeof(temp_file), "%s/%s-latest.zv", 
                 CACHE_DIR, package_name);
    }
    
    // Télécharger le package
    printf("📥 Downloading...\n");
    if (!http_download_file(url, temp_file, NULL)) {
        printf("❌ Failed to download package\n");
        return 0;
    }
    
    // Vérifier le fichier téléchargé
    struct stat st;
    if (stat(temp_file, &st) != 0 || st.st_size == 0) {
        printf("❌ Invalid package file\n");
        remove(temp_file);
        return 0;
    }
    
    printf("✅ Downloaded: %s (%.2f MB)\n", temp_file, st.st_size / (1024.0 * 1024.0));
    
    // Créer le répertoire d'installation
    char install_path[MAX_PATH];
    if (target_dir) {
        snprintf(install_path, sizeof(install_path), "%s/%s", target_dir, package_name);
    } else {
        snprintf(install_path, sizeof(install_path), "%s/%s", INSTALL_DIR, package_name);
    }
    
    create_directory(install_path);
    
    // Extraire le package
    printf("📦 Extracting to %s...\n", install_path);
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "tar -xzf %s -C %s 2>/dev/null", temp_file, install_path);
    
    int result = system(cmd);
    
    // Nettoyer
    remove(temp_file);
    
    if (result == 0) {
        // Vérifier l'installation
        char check_file[MAX_PATH];
        snprintf(check_file, sizeof(check_file), "%s/.zarch-manifest", install_path);
        
        if (file_exists(check_file)) {
            printf("✅ Package %s installed successfully!\n", package_name);
            
            // Afficher le manifeste
            FILE *fp = fopen(check_file, "r");
            if (fp) {
                char line[MAX_LINE];
                printf("\n📋 Package details:\n");
                while (fgets(line, sizeof(line), fp)) {
                    printf("   %s", line);
                }
                fclose(fp);
            }
            
            return 1;
        } else {
            printf("⚠️  Package installed but no manifest found\n");
            return 1;
        }
    } else {
        printf("❌ Failed to extract package\n");
        // Nettoyer le répertoire d'installation
        remove_directory(install_path);
        return 0;
    }
}

int uninstall_package(const char *package_name) {
    printf("🗑️  Uninstalling package: %s\n", package_name);
    
    char install_path[MAX_PATH];
    snprintf(install_path, sizeof(install_path), "%s/%s", INSTALL_DIR, package_name);
    
    if (!is_directory(install_path)) {
        printf("❌ Package not found: %s\n", package_name);
        return 0;
    }
    
    printf("📁 Removing directory: %s\n", install_path);
    
    if (remove_directory(install_path)) {
        printf("✅ Package %s uninstalled successfully!\n", package_name);
        return 1;
    } else {
        printf("❌ Failed to uninstall package\n");
        return 0;
    }
}

void list_installed_packages() {
    printf("📦 Installed packages in %s:\n", INSTALL_DIR);
    
    DIR *dir = opendir(INSTALL_DIR);
    if (!dir) {
        printf("❌ Cannot open install directory\n");
        return;
    }
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char package_path[MAX_PATH];
        snprintf(package_path, sizeof(package_path), "%s/%s", INSTALL_DIR, entry->d_name);
        
        if (is_directory(package_path)) {
            // Vérifier si c'est un package Zenv
            char manifest_path[MAX_PATH];
            snprintf(manifest_path, sizeof(manifest_path), "%s/.zarch-manifest", package_path);
            
            if (file_exists(manifest_path)) {
                printf("  • %s\n", entry->d_name);
                count++;
                
                // Lire la version du manifeste
                FILE *fp = fopen(manifest_path, "r");
                if (fp) {
                    char line[MAX_LINE];
                    while (fgets(line, sizeof(line), fp)) {
                        if (strstr(line, "Version:") == line) {
                            printf("    Version: %s", line + 9);
                            break;
                        }
                    }
                    fclose(fp);
                }
            }
        }
    }
    
    closedir(dir);
    
    if (count == 0) {
        printf("  No packages installed\n");
    } else {
        printf("\nTotal: %d packages\n", count);
    }
}

// ============================================================================
// COMMAND: SEARCH
// ============================================================================
void cmd_search(const char *query) {
    printf("🔍 Searching packages: %s\n", query ? query : "all");
    
    PackageList *result = search_packages(query);
    if (!result || result->count == 0) {
        printf("❌ No packages found\n");
        return;
    }
    
    printf("\n📦 Found %d packages:\n\n", result->count);
    
    for (int i = 0; i < result->count; i++) {
        printf("%3d. %-30s v%-10s\n", i+1, 
               result->packages[i].name, 
               result->packages[i].version);
        printf("     %s\n", result->packages[i].description);
        printf("     Author: %s | Downloads: %d | Size: %.2f MB\n\n",
               result->packages[i].author,
               result->packages[i].downloads,
               result->packages[i].size / (1024.0 * 1024.0));
    }
    
    free(result->packages);
}

// ============================================================================
// COMMAND: INFO
// ============================================================================
void cmd_info(const char *package_name) {
    printf("📋 Getting info for package: %s\n", package_name);
    
    PackageList *result = search_packages(package_name);
    if (!result || result->count == 0) {
        printf("❌ Package not found\n");
        return;
    }
    
    // Chercher le package exact
    for (int i = 0; i < result->count; i++) {
        if (strcmp(result->packages[i].name, package_name) == 0) {
            show_package_info(&result->packages[i]);
            
            // Afficher les URLs
            printf("🌐 URLs:\n");
            printf("  • Download: %s\n", result->packages[i].download_url);
            printf("  • README: %s\n", result->packages[i].readme_url);
            printf("  • License: %s\n", result->packages[i].license_url);
            printf("  • Docs: %s\n", result->packages[i].docs_url);
            
            free(result->packages);
            return;
        }
    }
    
    printf("❌ Package not found\n");
    free(result->packages);
}

// ============================================================================
// COMMAND: BUILD (AMÉLIORÉ)
// ============================================================================
void cmd_build(const char *config_file) {
    PackageConfig config;
    
    printf("🔨 Building package from: %s\n", config_file);
    
    if (!parse_json_config(config_file, &config)) {
        printf("❌ Invalid configuration file\n");
        return;
    }
    
    printf("\n📋 Package Configuration:\n");
    printf("  Name:        %s\n", config.name);
    printf("  Version:     %s\n", config.version);
    printf("  Author:      %s\n", config.author);
    printf("  License:     %s\n", config.license);
    printf("  Description: %s\n", config.description);
    printf("  Build dir:   %s\n", config.build_dir);
    printf("  Output:      %s\n", config.output_file);
    
    if (config.include_count > 0) {
        printf("  Includes (%d):\n", config.include_count);
        for (int i = 0; i < config.include_count; i++) {
            printf("    • %s\n", config.includes[i]);
        }
    }
    
    if (config.exclude_count > 0) {
        printf("  Excludes (%d):\n", config.exclude_count);
        for (int i = 0; i < config.exclude_count; i++) {
            printf("    • %s\n", config.excludes[i]);
        }
    }
    
    if (config.dep_count > 0) {
        printf("  Dependencies (%d):\n", config.dep_count);
        for (int i = 0; i < config.dep_count; i++) {
            printf("    • %s\n", config.dependencies[i]);
        }
    }
    
    if (config.script_count > 0) {
        printf("  Scripts (%d):\n", config.script_count);
        for (int i = 0; i < config.script_count; i++) {
            printf("    • %s\n", config.scripts[i]);
        }
    }
    
    printf("\n");
    
    if (!create_package_archive(&config)) {
        printf("❌ Build failed\n");
    }
    
    free_package_config(&config);
}

// ============================================================================
// COMMAND: PUBLISH (CORRIGÉ)
// ============================================================================
void cmd_publish(const char *package_file) {
    UserSession *session = load_session();
    
    if (strlen(session->token) == 0) {
        printf("❌ Not logged in. Use 'zarch login' first\n");
        return;
    }
    
    printf("🚀 Publishing package: %s\n", package_file);
    
    // Vérifier le fichier
    if (!file_exists(package_file)) {
        printf("❌ Package file not found\n");
        return;
    }
    
    // Extraire les métadonnées du package
    PackageConfig config;
    memset(&config, 0, sizeof(config));
    
    if (!extract_metadata_from_archive(package_file, &config)) {
        printf("⚠️  Could not extract metadata, using defaults\n");
    }
    
    // Vérifier aussi le fichier lock en parallèle (pour plus de fiabilité)
    char lock_file[MAX_PATH];
    char *base = strdup(package_file);
    char *ext = strrchr(base, '.');
    if (ext) *ext = '\0';
    
    snprintf(lock_file, sizeof(lock_file), "%s-lock.json", base);
    
    if (file_exists(lock_file)) {
        json_t *lock = json_load_file(lock_file, 0, NULL);
        if (lock) {
            json_t *name = json_object_get(lock, "name");
            json_t *ver = json_object_get(lock, "version");
            json_t *desc = json_object_get(lock, "description");
            json_t *auth = json_object_get(lock, "author");
            json_t *lic = json_object_get(lock, "license");
            
            if (name) strncpy(config.name, json_string_value(name), sizeof(config.name)-1);
            if (ver) strncpy(config.version, json_string_value(ver), sizeof(config.version)-1);
            if (desc) strncpy(config.description, json_string_value(desc), sizeof(config.description)-1);
            if (auth) strncpy(config.author, json_string_value(auth), sizeof(config.author)-1);
            if (lic) strncpy(config.license, json_string_value(lic), sizeof(config.license)-1);
            
            json_decref(lock);
        }
    }
    
    free(base);
    
    // Préparer la requête avec curl
    printf("📤 Uploading to Zenv Hub...\n");
    printf("📦 Package: %s v%s\n", config.name, config.version);
    printf("📝 Description: %s\n", config.description);
    printf("👤 Author: %s\n", config.author);
    printf("⚖️  License: %s\n", config.license);
    
    char curl_cmd[2000];
    snprintf(curl_cmd, sizeof(curl_cmd),
            "curl -s -X POST "
            "-H 'Authorization: zenv_%s' "
            "-F 'file=@%s' "
            "-F 'name=%s' "
            "-F 'version=%s' "
            "-F 'description=%s' "
            "-F 'author=%s' "
            "-F 'license=%s' "
            "%s/api/packages/upload",
            session->token, package_file, config.name, config.version, 
            config.description, config.author, config.license, HUB_URL);
    
    printf("\n⚙️  Executing upload command...\n");
    FILE *pipe = popen(curl_cmd, "r");
    if (pipe) {
        char response[4096];
        char full_response[8192] = "";
        
        while (fgets(response, sizeof(response), pipe)) {
            strcat(full_response, response);
        }
        
        int status = pclose(pipe);
        
        if (status == 0 && strlen(full_response) > 0) {
            json_t *root = json_loads(full_response, 0, NULL);
            if (root) {
                json_t *message = json_object_get(root, "message");
                json_t *pkg = json_object_get(root, "package");
                
                if (message) {
                    printf("✅ %s\n", json_string_value(message));
                }
                
                if (pkg) {
                    json_t *download_url = json_object_get(pkg, "download_url");
                    if (download_url) {
                        printf("🔗 Download URL: %s\n", json_string_value(download_url));
                    }
                }
                
                json_decref(root);
            } else {
                printf("✅ Package published successfully!\n");
            }
        } else {
            printf("❌ Failed to publish package\n");
            if (strlen(full_response) > 0) {
                printf("Error response: %s\n", full_response);
            }
        }
    }
}

// ============================================================================
// COMMAND: INSTALL (AMÉLIORÉ)
// ============================================================================
void cmd_install(const char *package_name, const char *version) {
    printf("📦 Installing package: %s", package_name);
    if (version) printf(" v%s", version);
    printf("\n");
    
    // Vérifier si le package est déjà installé
    char install_path[MAX_PATH];
    snprintf(install_path, sizeof(install_path), "%s/%s", INSTALL_DIR, package_name);
    
    if (is_directory(install_path)) {
        printf("⚠️  Package already installed at: %s\n", install_path);
        printf("💡 Use 'zarch reinstall %s' to reinstall\n", package_name);
        return;
    }
    
    // Installer le package
    if (!install_package(package_name, version, NULL)) {
        printf("❌ Installation failed\n");
    }
}

// ============================================================================
// COMMAND: UNINSTALL
// ============================================================================
void cmd_uninstall(const char *package_name) {
    if (!uninstall_package(package_name)) {
        printf("❌ Uninstall failed\n");
    }
}

// ============================================================================
// COMMAND: LIST
// ============================================================================
void cmd_list() {
    list_installed_packages();
}

// ============================================================================
// COMMAND: LOGIN
// ============================================================================
void cmd_login(const char *username, const char *password) {
    printf("🔐 Logging in to Zenv Hub...\n");
    
    char url[300];
    snprintf(url, sizeof(url), "%s/api/auth/login", HUB_URL);
    
    json_t *data = json_object();
    json_object_set_new(data, "username", json_string(username));
    json_object_set_new(data, "password", json_string(password));
    
    char *json_str = json_dumps(data, 0);
    json_decref(data);
    
    char *response = http_post_json(url, NULL, json_str);
    free(json_str);
    
    if (response) {
        json_t *root = json_loads(response, 0, NULL);
        free(response);
        
        if (root) {
            json_t *token_obj = json_object_get(root, "token");
            json_t *user_obj = json_object_get(root, "user");
            
            if (token_obj && user_obj) {
                json_t *access_token = json_object_get(token_obj, "access_token");
                json_t *user_data = json_object_get(user_obj, "username");
                json_t *role_data = json_object_get(user_obj, "role");
                
                if (access_token && user_data) {
                    UserSession session;
                    strncpy(session.token, json_string_value(access_token), sizeof(session.token)-1);
                    strncpy(session.username, json_string_value(user_data), sizeof(session.username)-1);
                    strncpy(session.role, role_data ? json_string_value(role_data) : "user", sizeof(session.role)-1);
                    session.expires = time(NULL) + 30 * 24 * 3600; // 30 jours
                    
                    save_session(&session);
                    
                    printf("✅ Logged in as %s (%s)\n", session.username, session.role);
                }
            } else {
                json_t *error = json_object_get(root, "error");
                if (error) {
                    printf("❌ Login failed: %s\n", json_string_value(error));
                } else {
                    printf("❌ Login failed\n");
                }
            }
            
            json_decref(root);
        }
    } else {
        printf("❌ Connection failed\n");
    }
}

// ============================================================================
// COMMAND: UPDATE
// ============================================================================
void cmd_update(const char *package_name) {
    printf("🔄 Updating package: %s\n", package_name);
    
    // Désinstaller d'abord
    if (!uninstall_package(package_name)) {
        printf("❌ Cannot uninstall old version\n");
        return;
    }
    
    // Installer la dernière version
    if (!install_package(package_name, NULL, NULL)) {
        printf("❌ Cannot install new version\n");
    }
}

// ============================================================================
// MAIN COMMAND DISPATCHER
// ============================================================================
void show_help() {
    printf("🐧 zarch v%s - Complete Package Manager\n", VERSION);
    printf("=============================================\n");
    printf("Commands:\n");
    printf("  build @za.json        Build package from JSON config\n");
    printf("  publish <file.zv>     Publish package to hub\n");
    printf("  install <pkg> [ver]   Install package from hub\n");
    printf("  uninstall <pkg>       Uninstall package\n");
    printf("  update <pkg>          Update package to latest version\n");
    printf("  list                  List installed packages\n");
    printf("  search [query]        Search packages in hub\n");
    printf("  info <pkg>            Show package information\n");
    printf("  login <user> <pass>   Login to Zenv Hub\n");
    printf("  logout                Logout from Zenv Hub\n");
    printf("  whoami                Show current user\n");
    printf("  help                  Show this help\n");
    printf("\nConfiguration file (@za.json):\n");
    printf("  {\n");
    printf("    \"name\": \"my-package\",\n");
    printf("    \"version\": \"1.0.0\",\n");
    printf("    \"author\": \"Your Name\",\n");
    printf("    \"license\": \"MIT\",\n");
    printf("    \"description\": \"My package\",\n");
    printf("    \"build_dir\": \".\",\n");
    printf("    \"output\": \"my-package-1.0.0.zv\",\n");
    printf("    \"include\": [\"src/\", \"README.md\"],\n");
    printf("    \"exclude\": [\".git\", \"*.tmp\"],\n");
    printf("    \"dependencies\": [\"other-package\"],\n");
    printf("    \"scripts\": {\n");
    printf("      \"test\": \"run-tests\",\n");
    printf("      \"install\": \"setup.sh\"\n");
    printf("    }\n");
    printf("  }\n");
}

void show_banner() {
    printf("\n");
    printf("███████╗ █████╗ ██████╗  ██████╗██╗  ██╗\n");
    printf("╚══███╔╝██╔══██╗██╔══██╗██╔════╝██║  ██║\n");
    printf("  ███╔╝ ███████║██████╔╝██║     ███████║\n");
    printf(" ███╔╝  ██╔══██║██╔══██╗██║     ██╔══██║\n");
    printf("███████╗██║  ██║██║  ██║╚██████╗██║  ██║\n");
    printf("╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝\n");
    printf("🐧 zarch v%s - Advanced Package Manager\n", VERSION);
    printf("🌐 Hub: %s\n", HUB_URL);
    printf("=========================================\n");
}

int main(int argc, char *argv[]) {
    show_banner();
    
    if (argc < 2) {
        show_help();
        return 0;
    }
    
    // Initialiser les répertoires
    create_directory("/etc/zarch");
    create_directory(CACHE_DIR);
    create_directory(INSTALL_DIR);
    
    if (strcmp(argv[1], "build") == 0) {
        if (argc < 3) {
            printf("❌ Usage: zarch build @za.json\n");
            return 1;
        }
        cmd_build(argv[2]);
        
    } else if (strcmp(argv[1], "publish") == 0) {
        if (argc < 3) {
            printf("❌ Usage: zarch publish <package.zv>\n");
            return 1;
        }
        cmd_publish(argv[2]);
        
    } else if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            printf("❌ Usage: zarch install <package> [version]\n");
            return 1;
        }
        const char *version = (argc >= 4) ? argv[3] : NULL;
        cmd_install(argv[2], version);
        
    } else if (strcmp(argv[1], "uninstall") == 0) {
        if (argc < 3) {
            printf("❌ Usage: zarch uninstall <package>\n");
            return 1;
        }
        cmd_uninstall(argv[2]);
        
    } else if (strcmp(argv[1], "update") == 0) {
        if (argc < 3) {
            printf("❌ Usage: zarch update <package>\n");
            return 1;
        }
        cmd_update(argv[2]);
        
    } else if (strcmp(argv[1], "list") == 0) {
        cmd_list();
        
    } else if (strcmp(argv[1], "search") == 0) {
        const char *query = (argc >= 3) ? argv[2] : NULL;
        cmd_search(query);
        
    } else if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            printf("❌ Usage: zarch info <package>\n");
            return 1;
        }
        cmd_info(argv[2]);
        
    } else if (strcmp(argv[1], "login") == 0) {
        if (argc < 4) {
            printf("❌ Usage: zarch login <username> <password>\n");
            return 1;
        }
        cmd_login(argv[2], argv[3]);
        
    } else if (strcmp(argv[1], "logout") == 0) {
        clear_session();
        
    } else if (strcmp(argv[1], "whoami") == 0) {
        UserSession *session = load_session();
        if (strlen(session->username) > 0) {
            printf("👤 User: %s\n", session->username);
            printf("🎭 Role: %s\n", session->role);
            printf("🔑 Token: %s...\n", session->token);
        } else {
            printf("❌ Not logged in\n");
        }
        
    } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        show_help();
        
    } else if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("zarch v%s\n", VERSION);
        
    } else {
        printf("❌ Unknown command: %s\n", argv[1]);
        printf("💡 Use: zarch help\n");
        return 1;
    }
    
    return 0;
}
