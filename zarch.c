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
#include <zlib.h>
#include <math.h>

#define VERSION "5.5.0"
#define REGISTRY_URL "https://zenv-hub.onrender.com"
#define CONFIG_DIR ".zarch"
#define CONFIG_FILE "config.json"
#define CACHE_FILE "cache.json"
#define LIB_PATH "/usr/local/bin/swiftvelox/addws"

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

typedef struct {
    char token[256];
    char username[64];
    char email[128];
    time_t last_update;
    char personal_code[16];
} Config;

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
    
    if (!ptr) return 0;
    
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
// BASE92 DECODING (ZARCH COMPATIBLE)
// ============================================================================

int decode_base92(const char* encoded, unsigned char** decoded, size_t* decoded_len) {
    // Base92 character set: A-Z a-z 0-9 !#$%&()*+,-./:;<=>?@[]^_`{|}~"
    const char* base92_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!#$%&()*+,-./:;<=>?@[]^_`{|}~\"";
    
    size_t in_len = strlen(encoded);
    *decoded_len = (in_len * 7) / 8; // Base92 uses 13 bits per 2 chars
    *decoded = malloc(*decoded_len + 1);
    
    if (!*decoded) {
        return 0;
    }
    
    size_t out_pos = 0;
    unsigned long value = 0;
    int bits = 0;
    
    for (size_t i = 0; i < in_len; i++) {
        char c = encoded[i];
        const char* pos = strchr(base92_chars, c);
        
        if (!pos) {
            // Caractère non-Base92, on saute
            continue;
        }
        
        int digit = pos - base92_chars;
        value = (value * 92) + digit;
        bits += 13; // Base92: 92^2 = 8464 < 8192 (2^13)
        
        while (bits >= 8) {
            bits -= 8;
            (*decoded)[out_pos++] = (value >> bits) & 0xFF;
        }
    }
    
    *decoded_len = out_pos;
    (*decoded)[out_pos] = '\0';
    
    return 1;
}

// Alternative: Base85 decoding (si le serveur utilise Base85)
int decode_base85(const char* encoded, unsigned char** decoded, size_t* decoded_len) {
    // Base85 (ASCII85) decoding
    size_t in_len = strlen(encoded);
    *decoded_len = (in_len * 4) / 5;
    *decoded = malloc(*decoded_len + 1);
    
    if (!*decoded) {
        return 0;
    }
    
    size_t out_pos = 0;
    unsigned long value = 0;
    int count = 0;
    
    for (size_t i = 0; i < in_len; i++) {
        char c = encoded[i];
        
        if (c < 33 || c > 117) {
            continue; // Skip invalid chars
        }
        
        value = value * 85 + (c - 33);
        count++;
        
        if (count == 5) {
            (*decoded)[out_pos++] = (value >> 24) & 0xFF;
            if (out_pos < *decoded_len) (*decoded)[out_pos++] = (value >> 16) & 0xFF;
            if (out_pos < *decoded_len) (*decoded)[out_pos++] = (value >> 8) & 0xFF;
            if (out_pos < *decoded_len) (*decoded)[out_pos++] = value & 0xFF;
            
            value = 0;
            count = 0;
        }
    }
    
    // Handle remaining bytes
    if (count > 0) {
        for (int i = 0; i < 5 - count; i++) {
            value = value * 85 + 84;
        }
        
        if (out_pos < *decoded_len) (*decoded)[out_pos++] = (value >> 24) & 0xFF;
        if (count > 1 && out_pos < *decoded_len) (*decoded)[out_pos++] = (value >> 16) & 0xFF;
        if (count > 2 && out_pos < *decoded_len) (*decoded)[out_pos++] = (value >> 8) & 0xFF;
        if (count > 3 && out_pos < *decoded_len) (*decoded)[out_pos++] = value & 0xFF;
    }
    
    *decoded_len = out_pos;
    (*decoded)[out_pos] = '\0';
    
    return 1;
}

// ============================================================================
// ZLIB DECOMPRESSION
// ============================================================================

int zlib_decompress(const unsigned char* compressed, size_t compressed_len, 
                    unsigned char** decompressed, size_t* decompressed_len) {
    if (compressed_len == 0) {
        return 0;
    }
    
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    
    if (inflateInit(&stream) != Z_OK) {
        return 0;
    }
    
    // Taille initiale estimée
    *decompressed_len = compressed_len * 5;
    *decompressed = malloc(*decompressed_len);
    
    if (!*decompressed) {
        inflateEnd(&stream);
        return 0;
    }
    
    stream.next_in = (Bytef*)compressed;
    stream.avail_in = compressed_len;
    stream.next_out = *decompressed;
    stream.avail_out = *decompressed_len;
    
    int ret = inflate(&stream, Z_FINISH);
    
    if (ret != Z_STREAM_END && ret != Z_OK) {
        free(*decompressed);
        inflateEnd(&stream);
        return 0;
    }
    
    *decompressed_len = stream.total_out;
    inflateEnd(&stream);
    
    return 1;
}

// ============================================================================
// ZARCH PACKAGE PROCESSING
// ============================================================================

int process_zarch_package(const char* zarch_content, const char* output_dir) {
    print_step("🔓", "Traitement du paquet Zarch...");
    
    // Essayer d'abord comme JSON
    json_t* root = json_loads(zarch_content, 0, NULL);
    const char* encoded_data = NULL;
    
    if (root) {
        // Format JSON avec champ 'content'
        encoded_data = json_string_value(json_object_get(root, "content"));
        if (!encoded_data) {
            // Pas de champ 'content', utiliser directement
            encoded_data = zarch_content;
        }
    } else {
        // Format brut (juste Base92)
        encoded_data = zarch_content;
    }
    
    if (!encoded_data || strlen(encoded_data) < 10) {
        if (root) json_decref(root);
        print_error("Données Zarch invalides");
        return 0;
    }
    
    printf("  Taille encodée: %zu chars\n", strlen(encoded_data));
    
    // Essayer Base92 d'abord, puis Base85
    unsigned char* decoded = NULL;
    size_t decoded_len = 0;
    int decode_success = 0;
    
    print_step("📝", "Décodage Base92/Base85...");
    
    // Essayer Base92
    decode_success = decode_base92(encoded_data, &decoded, &decoded_len);
    
    if (!decode_success || decoded_len == 0) {
        // Essayer Base85
        free(decoded);
        decoded = NULL;
        decode_success = decode_base85(encoded_data, &decoded, &decoded_len);
    }
    
    if (root) {
        json_decref(root);
    }
    
    if (!decode_success || decoded_len == 0) {
        print_error("Échec du décodage Base92/Base85");
        if (decoded) free(decoded);
        return 0;
    }
    
    printf("  Taille décodée: %zu bytes\n", decoded_len);
    
    // Vérifier si c'est compressé avec zlib
    print_step("🗜️", "Décompression zlib...");
    unsigned char* decompressed = NULL;
    size_t decompressed_len = 0;
    
    int decompress_success = zlib_decompress(decoded, decoded_len, &decompressed, &decompressed_len);
    free(decoded);
    
    if (!decompress_success || decompressed_len == 0) {
        // Pas de compression zlib, utiliser directement
        print_warning("Pas de compression zlib détectée");
        decompressed = decoded;
        decompressed_len = decoded_len;
    } else {
        printf("  Taille décompressée: %zu bytes\n", decompressed_len);
    }
    
    // Sauvegarder dans un fichier temporaire
    char temp_file[512];
    snprintf(temp_file, sizeof(temp_file), "/tmp/zarch_%ld.bin", time(NULL));
    
    FILE* f = fopen(temp_file, "wb");
    if (!f) {
        free(decompressed);
        print_error("Impossible de créer fichier temporaire");
        return 0;
    }
    
    size_t written = fwrite(decompressed, 1, decompressed_len, f);
    fclose(f);
    free(decompressed);
    
    if (written != decompressed_len) {
        print_error("Erreur d'écriture fichier temporaire");
        remove(temp_file);
        return 0;
    }
    
    // Essayer d'extraire comme tar.gz
    print_step("📦", "Extraction archive...");
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C \"%s\" 2>/dev/null", temp_file, output_dir);
    
    int extract_result = system(cmd);
    
    if (extract_result != 0) {
        print_warning("Format non-tar.gz, copie directe...");
        // Copier le fichier tel quel
        char dest_file[512];
        snprintf(dest_file, sizeof(dest_file), "%s/package.bin", output_dir);
        
        snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\"", temp_file, dest_file);
        if (system(cmd) != 0) {
            print_error("Échec copie fichier");
            remove(temp_file);
            return 0;
        }
    }
    
    // Nettoyer
    remove(temp_file);
    print_success("Paquet traité avec succès");
    return 1;
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
    if (!ensure_config_dir()) {
        return 0;
    }
    
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
    printf("  login <username> <password>    Login to registry\n");
    printf("  logout                         Logout\n");
    printf("  whoami                         Show current user\n");
    printf("  init                           Initialize new package\n");
    printf("  build [path]                   Build package\n");
    printf("  publish [path] [code]          Publish package\n");
    printf("  install <package>              Install package\n");
    printf("  uninstall <package>            Uninstall package\n");
    printf("  search [query]                 Search packages\n");
    printf("  info <package>                 Package info\n");
    printf("  list                           List installed\n");
    printf("  update                         Update index\n");
    printf("  version                        Show version\n");
    printf("\nOptions:\n");
    printf("  --scope=<scope>                Scope (user/org)\n");
    printf("  --force                        Force operation\n");
    printf("  --verbose                      Verbose mode\n");
    printf("  --no-cache                     Disable cache\n");
    printf("\nExamples:\n");
    printf("  zarch login john pass123\n");
    printf("  zarch init\n");
    printf("  zarch publish . CODE123\n");
    printf("  zarch install math\n");
    printf("  zarch search math\n");
}

void show_version() {
    printf("Zarch CLI v%s\n", VERSION);
    printf("Registry: %s\n", REGISTRY_URL);
    printf("Library Path: %s\n", LIB_PATH);
    printf("Support: Base92/Base85 + Zlib\n");
}

// --- LOGIN ---
int login_user(const char* username, const char* password) {
    print_step("🔐", "Login...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("CURL init failed");
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
                        print_success("Login successful!");
                        if (personal_code) {
                            printf(MAGENTA "🔒 Security code: %s\n" RESET, personal_code);
                        }
                    } else {
                        print_error("Config save failed");
                    }
                    
                    json_decref(resp);
                } else {
                    print_error("No token in response");
                }
            } else {
                print_error("Invalid JSON response");
            }
        } else {
            json_t *error = json_loads(chunk.memory, 0, NULL);
            if (error) {
                const char* err_msg = json_string_value(json_object_get(error, "error"));
                print_error(err_msg ? err_msg : "Login failed");
                json_decref(error);
            } else {
                print_error("HTTP error");
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
        print_success("Logged out");
    } else {
        print_error("Not logged in");
    }
}

// --- WHOAMI ---
void whoami() {
    Config config;
    if (load_config(&config)) {
        printf("👤 User: %s%s%s\n", GREEN, config.username, RESET);
        printf("🔒 Code: %s\n", config.personal_code[0] ? config.personal_code : "Not set");
    } else {
        print_error("Not logged in");
    }
}

// --- INIT ---
int init_package(const char* path) {
    print_step("🔄", "Initializing...");
    
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
    
    if (file_exists(manifest_path)) {
        print_warning("zarch.json already exists");
        return 0;
    }
    
    char name[128], version[32], description[256], author[128], license[32];
    
    printf("Package name: ");
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = 0;
    
    printf("Version (1.0.0): ");
    fgets(version, sizeof(version), stdin);
    version[strcspn(version, "\n")] = 0;
    if (strlen(version) == 0) strcpy(version, "1.0.0");
    
    printf("Description: ");
    fgets(description, sizeof(description), stdin);
    description[strcspn(description, "\n")] = 0;
    
    printf("Author: ");
    fgets(author, sizeof(author), stdin);
    author[strcspn(author, "\n")] = 0;
    
    printf("License (MIT): ");
    fgets(license, sizeof(license), stdin);
    license[strcspn(license, "\n")] = 0;
    if (strlen(license) == 0) strcpy(license, "MIT");
    
    json_t* root = json_object();
    json_object_set_new(root, "name", json_string(name));
    json_object_set_new(root, "version", json_string(version));
    json_object_set_new(root, "description", json_string(description));
    json_object_set_new(root, "author", json_string(author));
    json_object_set_new(root, "license", json_string(license));
    json_object_set_new(root, "scope", json_string("user"));
    
    char* json_str = json_dumps(root, JSON_INDENT(2));
    FILE* f = fopen(manifest_path, "w");
    if (!f) {
        json_decref(root);
        free(json_str);
        print_error("Error creating manifest");
        return 0;
    }
    
    fprintf(f, "%s", json_str);
    fclose(f);
    
    mkdir("src", 0755);
    
    FILE* readme = fopen("README.md", "w");
    if (readme) {
        fprintf(readme, "# %s\n\n%s\n", name, description);
        fclose(readme);
    }
    
    json_decref(root);
    free(json_str);
    
    print_success("Package initialized");
    return 1;
}

// --- BUILD ---
int build_package(const char* path, char* archive_out) {
    print_step("📦", "Building...");
    
    char manifest[512];
    snprintf(manifest, sizeof(manifest), "%s/zarch.json", path);
    
    if (!file_exists(manifest)) {
        print_error("zarch.json not found");
        return 0;
    }
    
    json_t* root = json_load_file(manifest, 0, NULL);
    if (!root) {
        print_error("Invalid manifest");
        return 0;
    }
    
    const char* name = json_string_value(json_object_get(root, "name"));
    const char* version = json_string_value(json_object_get(root, "version"));
    const char* scope = json_string_value(json_object_get(root, "scope"));
    
    if (!name || !version) {
        print_error("Name/version missing");
        json_decref(root);
        return 0;
    }
    
    if (!scope) scope = "user";
    
    snprintf(archive_out, 512, "/tmp/%s-%s-%s.tar.gz", scope, name, version);
    
    printf("  Name: %s\n", name);
    printf("  Version: %s\n", version);
    printf("  Scope: %s\n", scope);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -czf \"%s\" -C \"%s\" . 2>/dev/null", archive_out, path);
    
    if (system(cmd) != 0) {
        print_error("Archive creation failed");
        json_decref(root);
        return 0;
    }
    
    struct stat st;
    if (stat(archive_out, &st) == 0) {
        printf("  Size: %.2f KB\n", (double)st.st_size / 1024);
    }
    
    json_decref(root);
    print_success("Archive created");
    return 1;
}

// --- PUBLISH ---
int publish_package(const char* path, const char* personal_code) {
    print_step("🚀", "Publishing...");
    
    Config config;
    if (!load_config(&config)) {
        print_error("Not logged in. Use 'zarch login'");
        return 0;
    }
    
    if (!config.token[0]) {
        print_error("Token missing");
        return 0;
    }
    
    char archive_path[512];
    if (!build_package(path, archive_path)) {
        return 0;
    }
    
    char manifest[512];
    snprintf(manifest, sizeof(manifest), "%s/zarch.json", path);
    json_t* root = json_load_file(manifest, 0, NULL);
    if (!root) {
        print_error("Invalid manifest");
        return 0;
    }
    
    const char* name = json_string_value(json_object_get(root, "name"));
    const char* version = json_string_value(json_object_get(root, "version"));
    const char* scope = json_string_value(json_object_get(root, "scope"));
    const char* description = json_string_value(json_object_get(root, "description"));
    
    if (!scope) scope = "user";
    
    if (!personal_code || strlen(personal_code) < 4) {
        print_error("Security code required");
        json_decref(root);
        return 0;
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        json_decref(root);
        return 0;
    }
    
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/package/upload/%s/%s?token=%s", 
             REGISTRY_URL, scope, name, config.token);
    
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
    
    print_step("📤", "Uploading...");
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (http_code == 200) {
            json_t* resp = json_loads(chunk.memory, 0, NULL);
            if (resp) {
                const char* message = json_string_value(json_object_get(resp, "message"));
                print_success(message ? message : "Published!");
                
                json_t* details = json_object_get(resp, "details");
                if (details) {
                    printf("  📊 Details:\n");
                    const char* encryption = json_string_value(json_object_get(details, "encryption"));
                    int size_original = json_integer_value(json_object_get(details, "size_original"));
                    int size_secured = json_integer_value(json_object_get(details, "size_secured"));
                    
                    printf("     Encryption: %s\n", encryption);
                    printf("     Original: %.2f KB\n", size_original / 1024.0);
                    printf("     Secured: %.2f KB\n", size_secured / 1024.0);
                }
                
                json_decref(resp);
            }
        } else {
            json_t* error = json_loads(chunk.memory, 0, NULL);
            if (error) {
                const char* err_msg = json_string_value(json_object_get(error, "error"));
                print_error(err_msg ? err_msg : "Publish failed");
                json_decref(error);
            }
        }
    }
    
    curl_easy_cleanup(curl);
    curl_formfree(form);
    free(chunk.memory);
    json_decref(root);
    remove(archive_path);
    
    return res == CURLE_OK;
}

// --- INSTALL ---
int install_package(const char* pkg_name) {
    print_step("📥", "Installing...");
    printf("  Package: %s\n", pkg_name);
    
    char scope[64] = "user";
    char name[128];
    
    if (pkg_name[0] == '@') {
        char* slash = strchr(pkg_name, '/');
        if (slash) {
            strncpy(scope, pkg_name + 1, slash - pkg_name - 1);
            scope[slash - pkg_name - 1] = '\0';
            strncpy(name, slash + 1, sizeof(name) - 1);
        } else {
            print_error("Invalid format");
            return 0;
        }
    } else {
        strncpy(name, pkg_name, sizeof(name) - 1);
    }
    
    char target[512];
    snprintf(target, sizeof(target), "%s/%s", LIB_PATH, name);
    
    if (file_exists(target)) {
        print_warning("Already installed");
        printf("  Reinstall? [y/N]: ");
        char response[4];
        fgets(response, sizeof(response), stdin);
        if (response[0] != 'y' && response[0] != 'Y') {
            return 0;
        }
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", target);
        system(cmd);
    }
    
    char cmd_dir[512];
    snprintf(cmd_dir, sizeof(cmd_dir), "mkdir -p \"%s\"", target);
    if (system(cmd_dir) != 0) {
        print_error("Directory creation failed");
        return 0;
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        return 0;
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    char index_url[512];
    snprintf(index_url, sizeof(index_url), "%s/zarch/INDEX", REGISTRY_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, index_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        print_error("Index error");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    json_t *index = json_loads(chunk.memory, 0, NULL);
    if (!index) {
        print_error("Corrupted index");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    char full_name[256];
    if (strcmp(scope, "user") == 0) {
        snprintf(full_name, sizeof(full_name), "%s", name);
    } else {
        snprintf(full_name, sizeof(full_name), "@%s/%s", scope, name);
    }
    
    json_t *packages = json_object_get(index, "packages");
    json_t *pkg = json_object_get(packages, full_name);
    
    if (!pkg) {
        print_error("Package not found");
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    const char* version = json_string_value(json_object_get(pkg, "version"));
    printf("  Version: %s\n", version);
    
    char dl_url[1024];
    snprintf(dl_url, sizeof(dl_url), "%s/package/download/%s/%s/%s", 
             REGISTRY_URL, scope, name, version);
    
    free(chunk.memory);
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, dl_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    
    print_step("⬇️", "Downloading...");
    
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        print_error("Download failed");
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    if (chunk.size == 0) {
        print_error("Empty content");
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    // TRAITEMENT ZARCH CRITIQUE
    printf("  Received: %zu bytes\n", chunk.size);
    
    // Essayer de traiter comme Zarch
    if (!process_zarch_package(chunk.memory, target)) {
        print_error("Zarch processing failed");
        
        // Fallback: Sauvegarder le contenu brut
        char raw_file[512];
        snprintf(raw_file, sizeof(raw_file), "%s/package.raw", target);
        FILE* f = fopen(raw_file, "wb");
        if (f) {
            fwrite(chunk.memory, 1, chunk.size, f);
            fclose(f);
            print_warning("Saved raw content");
        }
    }
    
    json_decref(index);
    curl_easy_cleanup(curl);
    free(chunk.memory);
    
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", target);
    
    if (file_exists(manifest_path)) {
        print_success("Installation complete!");
        printf("  📍 Location: %s\n", target);
    } else {
        // Vérifier si des fichiers ont été extraits
        DIR* dir = opendir(target);
        if (dir) {
            struct dirent* entry;
            int file_count = 0;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] != '.') {
                    file_count++;
                }
            }
            closedir(dir);
            
            if (file_count > 0) {
                print_warning("Installed but no manifest");
                printf("  Files: %d\n", file_count);
            } else {
                print_error("Installation failed - empty directory");
            }
        } else {
            print_error("Installation failed - directory error");
        }
    }
    
    return 1;
}

// --- UNINSTALL ---
int uninstall_package(const char* pkg_name) {
    print_step("🗑️", "Uninstalling...");
    
    char target[512];
    snprintf(target, sizeof(target), "%s/%s", LIB_PATH, pkg_name);
    
    if (!file_exists(target)) {
        char target2[512];
        snprintf(target2, sizeof(target2), "%s/@%s", LIB_PATH, pkg_name);
        
        if (file_exists(target2)) {
            strcpy(target, target2);
        } else {
            print_error("Not found");
            return 0;
        }
    }
    
    printf("  Package: %s\n", pkg_name);
    printf("  Location: %s\n", target);
    printf("  Confirm? [y/N]: ");
    
    char response[4];
    fgets(response, sizeof(response), stdin);
    
    if (response[0] != 'y' && response[0] != 'Y') {
        print_info("Cancelled");
        return 0;
    }
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", target);
    
    if (system(cmd) == 0) {
        print_success("Uninstalled");
        return 1;
    } else {
        print_error("Uninstall failed");
        return 0;
    }
}

// --- SEARCH ---
void search_registry(const char* query) {
    print_step("🔍", "Searching...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("CURL init failed");
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
        print_error("Search failed");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    json_t *index = json_loads(chunk.memory, 0, NULL);
    if (!index) {
        print_error("Invalid index");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    json_t *packages = json_object_get(index, "packages");
    
    printf("\n┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ %-40s │ %-10s │ %-10s │\n", "PACKAGE", "VERSION", "SCOPE");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    
    const char* key;
    json_t *value;
    int found = 0;
    
    void *iter = json_object_iter(packages);
    while(iter) {
        key = json_object_iter_key(iter);
        value = json_object_iter_value(iter);
        
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
    printf("\nFound %d packages\n", found);
    
    json_decref(index);
    curl_easy_cleanup(curl);
    free(chunk.memory);
}

// --- LIST ---
void list_installed() {
    print_step("📁", "Installed packages...");
    
    DIR *dir = opendir(LIB_PATH);
    if (!dir) {
        print_error("Install directory not found");
        return;
    }
    
    printf("\n┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ %-40s │ %-20s │\n", "PACKAGE", "LOCATION");
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
    printf("\n%d packages installed\n", count);
}

// --- UPDATE ---
void update_index() {
    print_step("🔄", "Updating index...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("CURL init failed");
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
        print_error("Update failed");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    ensure_config_dir();
    char* cache_path = get_config_path(CACHE_FILE);
    FILE* f = fopen(cache_path, "w");
    if (f) {
        fwrite(chunk.memory, 1, chunk.size, f);
        fclose(f);
        print_success("Index updated");
    } else {
        print_error("Cache save failed");
    }
    
    curl_easy_cleanup(curl);
    free(chunk.memory);
}

// --- PARSING ARGUMENTS ---
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
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    Args args;
    parse_args(argc, argv, &args);
    
    if (argc > 1) {
        printf(CYAN BOLD "\n🐧 Zarch Package Manager v%s\n" RESET, VERSION);
    }
    
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
        // info function not fully implemented
        print_info("Info command - check registry website");
    } else if (strcmp(args.command, "list") == 0) {
        list_installed();
    } else if (strcmp(args.command, "update") == 0) {
        update_index();
    } else {
        print_error("Unknown command");
        show_help();
        return 1;
    }
    
    curl_global_cleanup();
    return 0;
}
