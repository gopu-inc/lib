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

#define VERSION "5.7.0"
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
    int auto_version;
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
// BASE85 DECODING
// ============================================================================

int decode_base85(const char* encoded, unsigned char** decoded, size_t* decoded_len) {
    if (!encoded || strlen(encoded) == 0) {
        return 0;
    }
    
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
            continue;
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
// ZARCH PACKAGE PROCESSING - VERSION CORRIGÉE
// ============================================================================

int process_zarch_package(const char* zarch_content, const char* output_dir) {
    print_step("🔓", "Traitement du paquet Zarch...");
    
    printf("  Taille reçue: %zu chars\n", strlen(zarch_content));
    
    // 1. Essayer de parser comme JSON
    json_t* root = json_loads(zarch_content, 0, NULL);
    const char* encoded_data = NULL;
    
    if (root) {
        encoded_data = json_string_value(json_object_get(root, "content"));
        if (!encoded_data) {
            encoded_data = zarch_content;
        }
        printf("  Format: JSON\n");
    } else {
        encoded_data = zarch_content;
        printf("  Format: Brut\n");
    }
    
    if (!encoded_data || strlen(encoded_data) < 10) {
        if (root) json_decref(root);
        print_error("Données invalides");
        return 0;
    }
    
    // 2. Décoder Base85
    print_step("📝", "Décodage Base85...");
    unsigned char* decoded = NULL;
    size_t decoded_len = 0;
    
    if (!decode_base85(encoded_data, &decoded, &decoded_len)) {
        if (root) json_decref(root);
        print_error("Échec décodage Base85");
        return 0;
    }
    
    if (root) {
        json_decref(root);
    }
    
    printf("  Taille décodée: %zu bytes\n", decoded_len);
    
    // 3. Vérifier le type de contenu
    int is_archive = 0;
    int is_text = 1;
    
    for (size_t i = 0; i < (decoded_len < 100 ? decoded_len : 100); i++) {
        if (decoded[i] < 32 && decoded[i] != '\n' && decoded[i] != '\t' && decoded[i] != '\r') {
            is_text = 0;
        }
        // Vérifier signature tar/gzip
        if (i < 2 && decoded[0] == 0x1F && decoded[1] == 0x8B) {
            is_archive = 1; // GZIP
        }
    }
    
    // 4. Essayer décompression zlib si pas déjà archive
    if (!is_archive) {
        print_step("🗜️", "Tentative décompression zlib...");
        unsigned char* decompressed = NULL;
        size_t decompressed_len = 0;
        
        if (zlib_decompress(decoded, decoded_len, &decompressed, &decompressed_len)) {
            printf("  Décompressé: %zu → %zu bytes\n", decoded_len, decompressed_len);
            free(decoded);
            decoded = decompressed;
            decoded_len = decompressed_len;
            
            // Re-vérifier si c'est une archive maintenant
            if (decoded_len >= 2 && decoded[0] == 0x1F && decoded[1] == 0x8B) {
                is_archive = 1;
            }
        } else {
            print_warning("Pas de compression zlib");
        }
    }
    
    // 5. Sauvegarder le contenu
    char output_path[512];
    snprintf(output_path, sizeof(output_path), "%s/package_content", output_dir);
    
    FILE* f = fopen(output_path, "wb");
    if (!f) {
        free(decoded);
        print_error("Impossible de créer fichier");
        return 0;
    }
    
    fwrite(decoded, 1, decoded_len, f);
    fclose(f);
    free(decoded);
    
    // 6. Essayer d'extraire si archive
    if (is_archive) {
        print_step("📦", "Extraction archive...");
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C \"%s\" 2>/dev/null", output_path, output_dir);
        
        if (system(cmd) == 0) {
            remove(output_path);
            print_success("Archive extraite");
        } else {
            // Essayer tar simple
            snprintf(cmd, sizeof(cmd), "tar -xf \"%s\" -C \"%s\" 2>/dev/null", output_path, output_dir);
            if (system(cmd) == 0) {
                remove(output_path);
                print_success("Archive tar extraite");
            } else {
                print_warning("Pas une archive, contenu brut sauvegardé");
            }
        }
    } else if (is_text) {
        print_step("📄", "Contenu texte détecté...");
        // Renommer en .txt pour plus de clarté
        char txt_path[512];
        snprintf(txt_path, sizeof(txt_path), "%s/package.txt", output_dir);
        rename(output_path, txt_path);
        print_success("Contenu texte sauvegardé");
    } else {
        print_step("💾", "Contenu binaire sauvegardé...");
        print_success("Fichier binaire sauvegardé");
    }
    
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
    if (username) strncpy(config->username, username, sizeof(config.username) - 1);
    if (email) strncpy(config->email, email, sizeof(config.email) - 1);
    if (personal_code) strncpy(config->personal_code, personal_code, sizeof(config.personal_code) - 1);
    
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
// FONCTIONS VERSION
// ============================================================================

void increment_version(char* version) {
    int major, minor, patch;
    if (sscanf(version, "%d.%d.%d", &major, &minor, &patch) == 3) {
        patch++;
        snprintf(version, 32, "%d.%d.%d", major, minor, patch);
    } else if (sscanf(version, "%d.%d", &major, &minor) == 2) {
        minor++;
        snprintf(version, 32, "%d.%d.0", major, minor);
    } else if (sscanf(version, "%d", &major) == 1) {
        major++;
        snprintf(version, 32, "%d.0.0", major);
    } else {
        snprintf(version, 32, "1.0.%ld", time(NULL) % 1000);
    }
}

// ============================================================================
// COMMANDES CLI
// ============================================================================

void show_help() {
    printf(BOLD "\n🐧 Zarch Package Manager v%s\n\n" RESET, VERSION);
    printf("Usage: zarch <command> [options]\n\n");
    printf("Commands:\n");
    printf("  login <username> <password>    Login\n");
    printf("  logout                         Logout\n");
    printf("  whoami                         Show user\n");
    printf("  init                           New package\n");
    printf("  build [path]                   Build\n");
    printf("  publish [path] [code]          Publish\n");
    printf("  install <package>              Install\n");
    printf("  uninstall <package>            Uninstall\n");
    printf("  remove <package>               Remove (alias)\n");
    printf("  search [query]                 Search\n");
    printf("  list                           List installed\n");
    printf("  update                         Update index\n");
    printf("  version                        Version\n");
    printf("\nOptions for publish:\n");
    printf("  --force                        Force overwrite\n");
    printf("  --auto-version                 Auto-increment version\n");
    printf("\nExamples:\n");
    printf("  zarch publish . CODE --auto-version\n");
    printf("  zarch install math\n");
    printf("  zarch search math\n");
}

void show_version() {
    printf("Zarch CLI v%s\n", VERSION);
    printf("Registry: %s\n", REGISTRY_URL);
    printf("Library: %s\n", LIB_PATH);
}

// --- LOGIN ---
int login_user(const char* username, const char* password) {
    print_step("🔐", "Login...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("CURL failed");
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
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
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
                    print_success("Login OK!");
                    if (personal_code) {
                        printf(MAGENTA "🔒 Code: %s\n" RESET, personal_code);
                    }
                }
                
                json_decref(resp);
            }
        }
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
    } else {
        print_error("Not logged in");
    }
}

// --- INIT ---
int init_package(const char* path) {
    print_step("🔄", "Init package...");
    
    char manifest[512];
    snprintf(manifest, sizeof(manifest), "%s/zarch.json", path);
    
    if (file_exists(manifest)) {
        print_warning("zarch.json exists");
        return 0;
    }
    
    char name[128], version[32], desc[256], author[128], license[32];
    
    printf("Name: ");
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = 0;
    
    printf("Version (1.0.0): ");
    fgets(version, sizeof(version), stdin);
    version[strcspn(version, "\n")] = 0;
    if (!version[0]) strcpy(version, "1.0.0");
    
    printf("Description: ");
    fgets(desc, sizeof(desc), stdin);
    desc[strcspn(desc, "\n")] = 0;
    
    printf("Author: ");
    fgets(author, sizeof(author), stdin);
    author[strcspn(author, "\n")] = 0;
    
    printf("License (MIT): ");
    fgets(license, sizeof(license), stdin);
    license[strcspn(license, "\n")] = 0;
    if (!license[0]) strcpy(license, "MIT");
    
    json_t* root = json_object();
    json_object_set_new(root, "name", json_string(name));
    json_object_set_new(root, "version", json_string(version));
    json_object_set_new(root, "description", json_string(desc));
    json_object_set_new(root, "author", json_string(author));
    json_object_set_new(root, "license", json_string(license));
    json_object_set_new(root, "scope", json_string("user"));
    
    char* json_str = json_dumps(root, JSON_INDENT(2));
    FILE* f = fopen(manifest, "w");
    if (f) {
        fprintf(f, "%s", json_str);
        fclose(f);
    }
    
    json_decref(root);
    free(json_str);
    
    mkdir("src", 0755);
    
    FILE* readme = fopen("README.md", "w");
    if (readme) {
        fprintf(readme, "# %s\n\n%s\n", name, desc);
        fclose(readme);
    }
    
    print_success("Package created");
    return 1;
}

// --- BUILD ---
int build_package(const char* path, char* archive_out, int auto_version) {
    print_step("📦", "Building...");
    
    char manifest[512];
    snprintf(manifest, sizeof(manifest), "%s/zarch.json", path);
    
    if (!file_exists(manifest)) {
        print_error("No zarch.json");
        return 0;
    }
    
    json_t* root = json_load_file(manifest, 0, NULL);
    if (!root) {
        print_error("Invalid manifest");
        return 0;
    }
    
    const char* name = json_string_value(json_object_get(root, "name"));
    const char* scope = json_string_value(json_object_get(root, "scope"));
    char version[32];
    
    if (auto_version) {
        const char* ver = json_string_value(json_object_get(root, "version"));
        strcpy(version, ver);
        increment_version(version);
        json_object_set_new(root, "version", json_string(version));
        
        char* new_json = json_dumps(root, JSON_INDENT(2));
        FILE* f = fopen(manifest, "w");
        if (f) {
            fprintf(f, "%s", new_json);
            fclose(f);
        }
        free(new_json);
        printf("  Version: %s → %s\n", ver, version);
    } else {
        const char* ver = json_string_value(json_object_get(root, "version"));
        if (ver) strcpy(version, ver);
        else strcpy(version, "1.0.0");
    }
    
    if (!scope) scope = "user";
    
    snprintf(archive_out, 512, "/tmp/%s-%s-%s.tar.gz", scope, name, version);
    
    printf("  Name: %s\n", name);
    printf("  Version: %s\n", version);
    printf("  Scope: %s\n", scope);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -czf \"%s\" -C \"%s\" . 2>/dev/null", archive_out, path);
    
    if (system(cmd) != 0) {
        print_error("Build failed");
        json_decref(root);
        return 0;
    }
    
    struct stat st;
    if (stat(archive_out, &st) == 0) {
        printf("  Size: %.2f KB\n", (double)st.st_size / 1024);
    }
    
    json_decref(root);
    print_success("Built");
    return 1;
}

// --- PUBLISH ---
int publish_package(const char* path, const char* personal_code, int force, int auto_version) {
    print_step("🚀", "Publishing...");
    
    Config config;
    if (!load_config(&config)) {
        print_error("Not logged in");
        return 0;
    }
    
    char archive[512];
    if (!build_package(path, archive, auto_version)) {
        return 0;
    }
    
    char manifest[512];
    snprintf(manifest, sizeof(manifest), "%s/zarch.json", path);
    json_t* root = json_load_file(manifest, 0, NULL);
    if (!root) {
        print_error("No manifest");
        return 0;
    }
    
    const char* name = json_string_value(json_object_get(root, "name"));
    const char* scope = json_string_value(json_object_get(root, "scope"));
    const char* version = json_string_value(json_object_get(root, "version"));
    const char* desc = json_string_value(json_object_get(root, "description"));
    
    if (!scope) scope = "user";
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        json_decref(root);
        return 0;
    }
    
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/package/upload/%s/%s?token=%s", 
             REGISTRY_URL, scope, name, config.token);
    
    if (force) {
        strcat(url, "&force=true");
    }
    
    struct curl_httppost *form = NULL;
    struct curl_httppost *last = NULL;
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "file",
                 CURLFORM_FILE, archive,
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "version",
                 CURLFORM_COPYCONTENTS, version,
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "description",
                 CURLFORM_COPYCONTENTS, desc ? desc : "",
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "license",
                 CURLFORM_COPYCONTENTS, json_string_value(json_object_get(root, "license")),
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "personal_code",
                 CURLFORM_COPYCONTENTS, personal_code,
                 CURLFORM_END);
    
    struct MemoryStruct chunk = {malloc(1), 0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    
    print_step("📤", "Uploading...");
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        print_success("Published!");
        printf("  Package: %s v%s\n", name, version);
    } else {
        print_error("Upload failed");
    }
    
    curl_easy_cleanup(curl);
    curl_formfree(form);
    free(chunk.memory);
    json_decref(root);
    remove(archive);
    
    return res == CURLE_OK;
}

// --- INSTALL - VERSION SIMPLIFIÉE ET CORRECTE ---
int install_package(const char* pkg_name) {
    print_step("📥", "Installing...");
    printf("  Package: %s\n", pkg_name);
    
    // Extraire scope et nom
    char scope[64] = "user";
    char name[128];
    
    if (pkg_name[0] == '@') {
        char* slash = strchr(pkg_name, '/');
        if (slash) {
            strncpy(scope, pkg_name + 1, slash - pkg_name - 1);
            scope[slash - pkg_name - 1] = 0;
            strncpy(name, slash + 1, sizeof(name) - 1);
        } else {
            strcpy(name, pkg_name + 1);
        }
    } else {
        strcpy(name, pkg_name);
    }
    
    // Créer dossier cible
    char target[512];
    snprintf(target, sizeof(target), "%s/%s", LIB_PATH, name);
    
    if (file_exists(target)) {
        print_warning("Already exists");
        printf("  Replace? [y/N]: ");
        char resp[4];
        fgets(resp, sizeof(resp), stdin);
        if (resp[0] != 'y' && resp[0] != 'Y') {
            return 0;
        }
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", target);
        system(cmd);
    }
    
    mkdir(target, 0755);
    
    // Obtenir URL de téléchargement depuis INDEX
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
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        print_error("Can't fetch index");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    json_t *index = json_loads(chunk.memory, 0, NULL);
    if (!index) {
        print_error("Bad index");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    // Chercher le package
    char full_name[256];
    if (strcmp(scope, "user") == 0) {
        snprintf(full_name, sizeof(full_name), "%s", name);
    } else {
        snprintf(full_name, sizeof(full_name), "@%s/%s", scope, name);
    }
    
    json_t *packages = json_object_get(index, "packages");
    json_t *pkg = json_object_get(packages, full_name);
    
    if (!pkg) {
        print_error("Not found in registry");
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    const char* version = json_string_value(json_object_get(pkg, "version"));
    printf("  Version: %s\n", version);
    
    // URL de téléchargement
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
    
    if (res != CURLE_OK || chunk.size == 0) {
        print_error("Download failed");
        json_decref(index);
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    
    printf("  Received: %zu bytes\n", chunk.size);
    
    // Sauvegarder le contenu brut d'abord pour inspection
    char raw_file[512];
    snprintf(raw_file, sizeof(raw_file), "%s/raw.zarch", target);
    FILE* f = fopen(raw_file, "w");
    if (f) {
        fwrite(chunk.memory, 1, chunk.size, f);
        fclose(f);
    }
    
    // Traiter avec notre fonction corrigée
    if (process_zarch_package(chunk.memory, target)) {
        print_success("Installed!");
        printf("  Location: %s\n", target);
        
        // Lister les fichiers
        DIR* dir = opendir(target);
        if (dir) {
            struct dirent* entry;
            printf("  Files:\n");
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] != '.') {
                    printf("    - %s\n", entry->d_name);
                }
            }
            closedir(dir);
        }
        
        // Supprimer le fichier raw
        remove(raw_file);
    } else {
        print_warning("Raw content saved in raw.zarch");
    }
    
    json_decref(index);
    curl_easy_cleanup(curl);
    free(chunk.memory);
    
    return 1;
}

// --- UNINSTALL ---
int uninstall_package(const char* pkg_name) {
    print_step("🗑️", "Uninstalling...");
    
    char target[512];
    snprintf(target, sizeof(target), "%s/%s", LIB_PATH, pkg_name);
    
    if (!file_exists(target)) {
        print_error("Not found");
        return 0;
    }
    
    printf("  Package: %s\n", pkg_name);
    printf("  Location: %s\n", target);
    printf("  Confirm? [y/N]: ");
    
    char resp[4];
    fgets(resp, sizeof(resp), stdin);
    
    if (resp[0] != 'y' && resp[0] != 'Y') {
        print_info("Cancelled");
        return 0;
    }
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", target);
    
    if (system(cmd) == 0) {
        print_success("Uninstalled");
        return 1;
    } else {
        print_error("Failed");
        return 0;
    }
}

// --- SEARCH ---
void search_registry(const char* query) {
    print_step("🔍", "Searching...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("CURL failed");
        return;
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    char url[512];
    snprintf(url, sizeof(url), "%s/zarch/INDEX", REGISTRY_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        print_error("Search failed");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    json_t *index = json_loads(chunk.memory, 0, NULL);
    if (!index) {
        print_error("Bad index");
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return;
    }
    
    json_t *packages = json_object_get(index, "packages");
    
    printf("\nPackages:\n");
    printf("----------\n");
    
    const char* key;
    json_t *value;
    int found = 0;
    
    void *iter = json_object_iter(packages);
    while(iter) {
        key = json_object_iter_key(iter);
        value = json_object_iter_value(iter);
        
        if (!query || strstr(key, query)) {
            printf("%s v%s (%s)\n", 
                   key,
                   json_string_value(json_object_get(value, "version")),
                   json_string_value(json_object_get(value, "scope")));
            found++;
        }
        
        iter = json_object_iter_next(packages, iter);
    }
    
    printf("\nFound: %d\n", found);
    
    json_decref(index);
    curl_easy_cleanup(curl);
    free(chunk.memory);
}

// --- LIST ---
void list_installed() {
    print_step("📁", "Installed...");
    
    DIR *dir = opendir(LIB_PATH);
    if (!dir) {
        print_error("No lib directory");
        return;
    }
    
    struct dirent *entry;
    int count = 0;
    
    printf("\n");
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", LIB_PATH, entry->d_name);
            
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                printf("• %s\n", entry->d_name);
                count++;
            }
        }
    }
    
    closedir(dir);
    printf("\nTotal: %d packages\n", count);
}

// --- UPDATE ---
void update_index() {
    print_step("🔄", "Updating...");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        print_error("CURL failed");
        return;
    }
    
    struct MemoryStruct chunk = {malloc(1), 0};
    char url[512];
    snprintf(url, sizeof(url), "%s/zarch/INDEX", REGISTRY_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        print_success("Updated");
    } else {
        print_error("Failed");
    }
    
    curl_easy_cleanup(curl);
    free(chunk.memory);
}

// --- PARSING ARGS ---
void parse_args(int argc, char** argv, Args* args) {
    memset(args, 0, sizeof(Args));
    strcpy(args->path, ".");
    
    if (argc < 2) return;
    
    strncpy(args->command, argv[1], sizeof(args->command) - 1);
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--force") == 0) args->force = 1;
        else if (strcmp(argv[i], "--auto-version") == 0) args->auto_version = 1;
        else if (strcmp(argv[i], "--verbose") == 0) args->verbose = 1;
        else if (strncmp(argv[i], "--scope=", 8) == 0) {
            strncpy(args->scope, argv[i] + 8, sizeof(args->scope) - 1);
        }
        else if (i == 2) {
            if (strcmp(args->command, "login") == 0) {
                strncpy(args->username, argv[i], sizeof(args->username) - 1);
            }
            else if (strcmp(args->command, "publish") == 0) {
                if (argv[i][0] != '-') strncpy(args->path, argv[i], sizeof(args->path) - 1);
            }
            else if (strcmp(args->command, "install") == 0 || 
                    strcmp(args->command, "uninstall") == 0 ||
                    strcmp(args->command, "remove") == 0 ||
                    strcmp(args->command, "search") == 0) {
                strncpy(args->package_name, argv[i], sizeof(args->package_name) - 1);
            }
            else if (strcmp(args->command, "build") == 0) {
                strncpy(args->path, argv[i], sizeof(args->path) - 1);
            }
        }
        else if (i == 3) {
            if (strcmp(args->command, "login") == 0) {
                strncpy(args->password, argv[i], sizeof(args->password) - 1);
            }
            else if (strcmp(args->command, "publish") == 0) {
                if (argv[i][0] != '-') strncpy(args->personal_code, argv[i], sizeof(args->personal_code) - 1);
            }
        }
        else if (i == 4 && strcmp(args->command, "publish") == 0) {
            strncpy(args->personal_code, argv[i], sizeof(args->personal_code) - 1);
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
            print_error("Usage: zarch login <user> <pass>");
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
        build_package(args.path, archive, 0);
    } else if (strcmp(args.command, "publish") == 0) {
        int has_code = 0;
        char code[128] = {0};
        
        // Trouver le code (dernier arg non-option)
        for (int i = argc - 1; i >= 2; i--) {
            if (argv[i][0] != '-') {
                strcpy(code, argv[i]);
                has_code = 1;
                break;
            }
        }
        
        if (!has_code) {
            print_error("Need personal code: zarch publish [path] CODE [--force] [--auto-version]");
            return 1;
        }
        
        publish_package(args.path, code, args.force, args.auto_version);
    } else if (strcmp(args.command, "install") == 0) {
        if (argc < 3) {
            print_error("Usage: zarch install <package>");
            return 1;
        }
        install_package(args.package_name);
    } else if (strcmp(args.command, "uninstall") == 0 || strcmp(args.command, "remove") == 0) {
        if (argc < 3) {
            print_error("Usage: zarch uninstall <package>");
            return 1;
        }
        uninstall_package(args.package_name);
    } else if (strcmp(args.command, "search") == 0) {
        search_registry(argc >= 3 ? args.package_name : NULL);
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
