/*
 * Zarch Package Manager - Version améliorée
 * Gestionnaire CLI pour les paquets .svlib (SwiftVelox)
 * Compilation: gcc -o zarch zarch.c -lcurl -ljansson -lz -lcrypto
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>

/* Librairies externes */
#include <curl/curl.h>
#include <jansson.h>
#include <zlib.h>
#include <openssl/sha.h>

/* ============================================================================
 * CONFIGURATION ET DÉFINITIONS
 * ============================================================================ */

#define VERSION "3.0.0"
#define USER_AGENT "Zarch-CLI/" VERSION
#define CONFIG_DIR ".zarch"
#define CACHE_DIR ".zarch/cache"
#define CACHE_EXPIRE_SEC (3600 * 24) /* 24h */

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_CYAN    "\033[1;36m"

#define ICON_SUCCESS "✅"
#define ICON_ERROR   "❌"
#define ICON_WARN    "⚠️"
#define ICON_INFO    "ℹ️"
#define ICON_DOWNLOAD "⬇️"
#define ICON_EXTRACT  "📦"

/* Types de paquets supportés */
typedef enum {
    PKG_TYPE_SVLIB,
    PKG_TYPE_ZARCH,
    PKG_TYPE_UNKNOWN
} PackageType;

/* Structure de configuration utilisateur */
typedef struct {
    char token[256];
    char username[64];
    char email[128];
    char server_url[256];
    char personal_code[16];
    time_t last_update;
    int use_cache;
    int verbose;
} UserConfig;

/* Structure pour le téléchargement */
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    int http_status;
} DownloadBuffer;

/* Structure d'un paquet */
typedef struct {
    char name[128];
    char scope[64];
    char version[32];
    char author[128];
    char description[256];
    char license[32];
    char sha256[65];
    char signature[129];
    time_t created_at;
    time_t updated_at;
    long download_count;
    PackageType type;
    int swiftvelox_compatible;
} PackageInfo;

/* ============================================================================
 * FONCTIONS UTILITAIRES (LOGGING, CONFIG, FICHIERS)
 * ============================================================================ */

void log_error(const char *format, ...) {
    va_list args;
    fprintf(stderr, COLOR_RED ICON_ERROR " ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, COLOR_RESET "\n");
}

void log_success(const char *format, ...) {
    va_list args;
    printf(COLOR_GREEN ICON_SUCCESS " ");
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf(COLOR_RESET "\n");
}

void log_info(const char *format, ...) {
    va_list args;
    printf(COLOR_CYAN ICON_INFO " ");
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf(COLOR_RESET "\n");
}

void log_debug(const char *format, ...) {
    if (getenv("ZARCH_DEBUG")) {
        va_list args;
        printf(COLOR_MAGENTA "[DEBUG] ");
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        printf(COLOR_RESET "\n");
    }
}

/* Obtient le chemin du fichier de config */
char* get_config_path(char *buffer, size_t size, const char *filename) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buffer, size, "%s/%s/%s", home, CONFIG_DIR, filename);
    return buffer;
}

/* Crée les répertoires de config si nécessaire */
int ensure_config_dirs(void) {
    char config_dir[PATH_MAX];
    char cache_dir[PATH_MAX];
    
    get_config_path(config_dir, sizeof(config_dir), "");
    get_config_path(cache_dir, sizeof(cache_dir), "cache");
    
    if (mkdir(config_dir, 0700) && errno != EEXIST) {
        log_error("Impossible de créer %s: %s", config_dir, strerror(errno));
        return 0;
    }
    
    if (mkdir(cache_dir, 0700) && errno != EEXIST) {
        log_error("Impossible de créer %s: %s", cache_dir, strerror(errno));
        return 0;
    }
    
    return 1;
}

/* Charge la configuration utilisateur */
int load_config(UserConfig *config) {
    char config_path[PATH_MAX];
    FILE *fp;
    json_t *root;
    json_error_t error;
    
    get_config_path(config_path, sizeof(config_path), "config.json");
    
    if (!(fp = fopen(config_path, "r"))) {
        return 0;
    }
    
    root = json_loadf(fp, 0, &error);
    fclose(fp);
    
    if (!root) {
        log_debug("Erreur JSON ligne %d: %s", error.line, error.text);
        return 0;
    }
    
    /* Extraction des champs avec valeurs par défaut */
    const char *token = json_string_value(json_object_get(root, "token"));
    const char *username = json_string_value(json_object_get(root, "username"));
    const char *email = json_string_value(json_object_get(root, "email"));
    const char *server = json_string_value(json_object_get(root, "server_url"));
    const char *code = json_string_value(json_object_get(root, "personal_code"));
    
    if (token) strncpy(config->token, token, sizeof(config->token)-1);
    if (username) strncpy(config->username, username, sizeof(config->username)-1);
    if (email) strncpy(config->email, email, sizeof(config->email)-1);
    if (code) strncpy(config->personal_code, code, sizeof(config->personal_code)-1);
    
    strncpy(config->server_url, 
            server ? server : "https://zenv-hub.onrender.com",
            sizeof(config->server_url)-1);
    
    config->last_update = json_integer_value(json_object_get(root, "last_update"));
    config->use_cache = json_is_true(json_object_get(root, "use_cache"));
    config->verbose = json_is_true(json_object_get(root, "verbose"));
    
    json_decref(root);
    return 1;
}

/* Sauvegarde la configuration */
int save_config(const UserConfig *config) {
    char config_path[PATH_MAX];
    FILE *fp;
    json_t *root;
    
    if (!ensure_config_dirs()) {
        return 0;
    }
    
    get_config_path(config_path, sizeof(config_path), "config.json");
    
    root = json_object();
    json_object_set_new(root, "token", json_string(config->token));
    json_object_set_new(root, "username", json_string(config->username));
    json_object_set_new(root, "email", json_string(config->email));
    json_object_set_new(root, "server_url", json_string(config->server_url));
    json_object_set_new(root, "personal_code", json_string(config->personal_code));
    json_object_set_new(root, "last_update", json_integer(time(NULL)));
    json_object_set_new(root, "use_cache", json_boolean(config->use_cache));
    json_object_set_new(root, "verbose", json_boolean(config->verbose));
    
    if (!(fp = fopen(config_path, "w"))) {
        json_decref(root);
        log_error("Impossible d'écrire %s: %s", config_path, strerror(errno));
        return 0;
    }
    
    json_dumpf(root, fp, JSON_INDENT(2));
    fclose(fp);
    json_decref(root);
    
    return 1;
}

/* Vérifie si un fichier existe et est lisible */
int file_exists(const char *path) {
    return access(path, R_OK) == 0;
}

/* Calcule le SHA256 d'un buffer */
void compute_sha256(const unsigned char *data, size_t len, char *output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, len);
    SHA256_Final(hash, &sha256);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[64] = '\0';
}

/* ============================================================================
 * GESTION DU TÉLÉCHARGEMENT (CURL)
 * ============================================================================ */

/* Callback pour écrire les données téléchargées */
static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    DownloadBuffer *buffer = (DownloadBuffer *)userdata;
    size_t realsize = size * nmemb;
    
    /* Vérifie si on a besoin de plus de mémoire */
    if (buffer->size + realsize + 1 > buffer->capacity) {
        size_t new_capacity = buffer->capacity * 2;
        if (new_capacity < buffer->size + realsize + 1) {
            new_capacity = buffer->size + realsize + 1;
        }
        
        char *new_data = realloc(buffer->data, new_capacity);
        if (!new_data) {
            log_error("Échec d'allocation mémoire pendant le téléchargement");
            return 0;
        }
        
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }
    
    /* Copie les données */
    memcpy(buffer->data + buffer->size, ptr, realsize);
    buffer->size += realsize;
    buffer->data[buffer->size] = '\0';
    
    return realsize;
}

/* Callback pour les headers (récupère le status HTTP) */
static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    DownloadBuffer *dl = (DownloadBuffer *)userdata;
    size_t realsize = size * nitems;
    
    /* Cherche le status HTTP */
    if (strncmp(buffer, "HTTP/", 5) == 0) {
        sscanf(buffer, "HTTP/%*d.%*d %d", &dl->http_status);
    }
    
    return realsize;
}

/* Télécharge une URL avec gestion d'erreurs améliorée */
DownloadBuffer* download_url(const char *url, const char *token, int timeout) {
    CURL *curl;
    CURLcode res;
    DownloadBuffer *buffer;
    
    if (!(curl = curl_easy_init())) {
        log_error("Impossible d'initialiser cURL");
        return NULL;
    }
    
    /* Initialise le buffer de téléchargement */
    buffer = calloc(1, sizeof(DownloadBuffer));
    if (!buffer) {
        curl_easy_cleanup(curl);
        return NULL;
    }
    
    buffer->capacity = 4096;
    buffer->data = malloc(buffer->capacity);
    if (!buffer->data) {
        free(buffer);
        curl_easy_cleanup(curl);
        return NULL;
    }
    
    /* Configuration cURL */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    
    /* Ajoute le token si fourni */
    if (token && token[0]) {
        struct curl_slist *headers = NULL;
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        buffer->http_status = 0; /* Sera mis à jour par le callback */
    }
    
    log_debug("Téléchargement de %s", url);
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        log_error("Échec du téléchargement: %s", curl_easy_strerror(res));
        free(buffer->data);
        free(buffer);
        buffer = NULL;
    } else if (buffer->http_status >= 400) {
        log_error("Erreur HTTP %d", buffer->http_status);
        free(buffer->data);
        free(buffer);
        buffer = NULL;
    }
    
    curl_easy_cleanup(curl);
    return buffer;
}

/* Libère un buffer de téléchargement */
void download_buffer_free(DownloadBuffer *buffer) {
    if (buffer) {
        free(buffer->data);
        free(buffer);
    }
}

/* ============================================================================
 * TRAITEMENT DES PAQUETS .svlib
 * ============================================================================ */

/* Valide la structure d'un package .svlib */
int validate_svlib_structure(const char *archive_path, PackageInfo *info) {
    char command[1024];
    char temp_dir[PATH_MAX];
    char manifest_path[PATH_MAX];
    FILE *fp;
    json_t *root;
    json_error_t error;
    
    /* Crée un répertoire temporaire */
    snprintf(temp_dir, sizeof(temp_dir), "/tmp/zarch_%ld", (long)getpid());
    if (mkdir(temp_dir, 0700) && errno != EEXIST) {
        log_error("Impossible de créer le répertoire temporaire");
        return 0;
    }
    
    /* Extrait l'archive */
    snprintf(command, sizeof(command),
             "tar -xzf \"%s\" -C \"%s\" 2>/dev/null",
             archive_path, temp_dir);
    
    if (system(command) != 0) {
        log_error("Archive invalide ou corrompue");
        return 0;
    }
    
    /* Vérifie la présence des fichiers requis */
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", temp_dir);
    if (!file_exists(manifest_path)) {
        log_error("Fichier zarch.json manquant");
        return 0;
    }
    
    /* Lit et valide le manifeste */
    if (!(fp = fopen(manifest_path, "r"))) {
        log_error("Impossible de lire zarch.json");
        return 0;
    }
    
    root = json_loadf(fp, 0, &error);
    fclose(fp);
    
    if (!root) {
        log_error("zarch.json invalide (ligne %d): %s", error.line, error.text);
        return 0;
    }
    
    /* Récupère les champs obligatoires */
    const char *name = json_string_value(json_object_get(root, "name"));
    const char *version = json_string_value(json_object_get(root, "version"));
    const char *scope = json_string_value(json_object_get(root, "scope"));
    const char *author = json_string_value(json_object_get(root, "author"));
    
    if (!name || !version) {
        log_error("Champs 'name' ou 'version' manquants dans zarch.json");
        json_decref(root);
        return 0;
    }
    
    /* Remplit la structure d'information */
    strncpy(info->name, name, sizeof(info->name)-1);
    strncpy(info->version, version, sizeof(info->version)-1);
    strncpy(info->scope, scope ? scope : "user", sizeof(info->scope)-1);
    if (author) strncpy(info->author, author, sizeof(info->author)-1);
    
    /* Champs optionnels */
    const char *desc = json_string_value(json_object_get(root, "description"));
    const char *license = json_string_value(json_object_get(root, "license"));
    
    if (desc) strncpy(info->description, desc, sizeof(info->description)-1);
    if (license) strncpy(info->license, license, sizeof(info->license)-1);
    else strcpy(info->license, "MIT");
    
    info->swiftvelox_compatible = json_is_true(
        json_object_get(root, "swiftvelox_compatible"));
    
    /* Vérifie la présence du répertoire src/ */
    char src_dir[PATH_MAX];
    snprintf(src_dir, sizeof(src_dir), "%s/src", temp_dir);
    DIR *dir = opendir(src_dir);
    if (!dir) {
        log_warn("Répertoire 'src/' manquant (package peut-être vide)");
    } else {
        closedir(dir);
    }
    
    /* Nettoie le répertoire temporaire */
    snprintf(command, sizeof(command), "rm -rf \"%s\"", temp_dir);
    system(command);
    
    json_decref(root);
    return 1;
}

/* Traite un package Zarch (Base85 + zlib) */
int process_zarch_package(const char *zarch_content, size_t content_len,
                         const char *output_dir, PackageInfo *info) {
    log_info("Traitement du paquet Zarch...");
    
    /* Tentative de parsing JSON */
    json_t *root = json_loadb(zarch_content, content_len, 0, NULL);
    const char *encoded_data = NULL;
    
    if (root) {
        encoded_data = json_string_value(json_object_get(root, "content"));
        if (!encoded_data) {
            encoded_data = zarch_content;
        }
    } else {
        encoded_data = zarch_content;
    }
    
    /* Décodage Base85 simplifié */
    size_t decoded_len = (strlen(encoded_data) * 4) / 5;
    unsigned char *decoded = malloc(decoded_len + 1);
    if (!decoded) {
        log_error("Échec d'allocation pour le décodage");
        if (root) json_decref(root);
        return 0;
    }
    
    /* Note: Implémentation complète du décodage Base85 nécessaire */
    log_warn("Décodage Base85 simplifié - implémentation complète requise");
    
    /* Décompression zlib */
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    
    if (inflateInit(&stream) != Z_OK) {
        log_error("Échec d'initialisation zlib");
        free(decoded);
        if (root) json_decref(root);
        return 0;
    }
    
    size_t decompressed_len = decoded_len * 5; /* Estimation */
    unsigned char *decompressed = malloc(decompressed_len);
    
    stream.next_in = decoded;
    stream.avail_in = decoded_len;
    stream.next_out = decompressed;
    stream.avail_out = decompressed_len;
    
    int ret = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    
    if (ret != Z_STREAM_END && ret != Z_OK) {
        log_error("Échec de décompression zlib: %d", ret);
        free(decompressed);
        free(decoded);
        if (root) json_decref(root);
        return 0;
    }
    
    decompressed_len = stream.total_out;
    
    /* Sauvegarde du contenu */
    char output_path[PATH_MAX];
    snprintf(output_path, sizeof(output_path), "%s/package_content", output_dir);
    
    FILE *f = fopen(output_path, "wb");
    if (!f) {
        log_error("Impossible de créer le fichier de sortie");
        free(decompressed);
        free(decoded);
        if (root) json_decref(root);
        return 0;
    }
    
    fwrite(decompressed, 1, decompressed_len, f);
    fclose(f);
    
    /* Nettoie la mémoire */
    free(decompressed);
    free(decoded);
    if (root) json_decref(root);
    
    /* Extrait si c'est une archive */
    if (decompressed_len >= 2 && decompressed[0] == 0x1F && decompressed[1] == 0x8B) {
        log_info("Extraction de l'archive...");
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C \"%s\" 2>/dev/null",
                output_path, output_dir);
        
        if (system(cmd) == 0) {
            remove(output_path);
            log_success("Archive extraite avec succès");
        } else {
            log_warn("Échec de l'extraction, contenu brut conservé");
        }
    }
    
    return 1;
}

/* ============================================================================
 * COMMANDES PRINCIPALES
 * ============================================================================ */

int cmd_login(const char *username, const char *password) {
    UserConfig config = {0};
    char url[512];
    DownloadBuffer *buffer;
    json_t *root;
    json_error_t error;
    
    log_info("Connexion en tant que %s...", username);
    
    /* Construction de l'URL */
    snprintf(url, sizeof(url), "%s/api/auth/login", config.server_url);
    
    /* Préparation de la requête JSON */
    json_t *req = json_object();
    json_object_set_new(req, "username", json_string(username));
    json_object_set_new(req, "password", json_string(password));
    
    char *json_str = json_dumps(req, 0);
    json_decref(req);
    
    /* Téléchargement (POST simplifié) */
    CURL *curl = curl_easy_init();
    if (!curl) {
        free(json_str);
        return 0;
    }
    
    /* Configuration pour POST */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    
    DownloadBuffer resp = {0};
    resp.capacity = 4096;
    resp.data = malloc(resp.capacity);
    
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(json_str);
    
    if (res != CURLE_OK || resp.size == 0) {
        log_error("Échec de la connexion au serveur");
        free(resp.data);
        return 0;
    }
    
    /* Parse la réponse */
    root = json_loads(resp.data, 0, &error);
    free(resp.data);
    
    if (!root) {
        log_error("Réponse serveur invalide: %s", error.text);
        return 0;
    }
    
    const char *token = json_string_value(json_object_get(root, "token"));
    const char *personal_code = json_string_value(json_object_get(root, "personal_code"));
    
    if (!token) {
        log_error("Token manquant dans la réponse");
        json_decref(root);
        return 0;
    }
    
    /* Sauvegarde la configuration */
    strncpy(config.username, username, sizeof(config.username)-1);
    strncpy(config.token, token, sizeof(config.token)-1);
    if (personal_code) {
        strncpy(config.personal_code, personal_code, sizeof(config.personal_code)-1);
    }
    
    if (save_config(&config)) {
        log_success("Connecté avec succès!");
        if (personal_code) {
            printf(COLOR_MAGENTA "🔒 Code personnel: %s\n" COLOR_RESET, personal_code);
        }
    } else {
        log_error("Échec de la sauvegarde de la configuration");
        json_decref(root);
        return 0;
    }
    
    json_decref(root);
    return 1;
}

int cmd_install(const char *package_spec) {
    UserConfig config;
    PackageInfo pkg_info = {0};
    char url[1024];
    char target_dir[PATH_MAX];
    DownloadBuffer *buffer;
    
    if (!load_config(&config)) {
        log_error("Non connecté. Utilisez 'zarch login' d'abord.");
        return 0;
    }
    
    log_info("Installation de %s...", package_spec);
    
    /* Parse package_spec (format: @scope/name ou name) */
    char scope[64] = "user";
    char name[128];
    
    if (package_spec[0] == '@') {
        char *slash = strchr(package_spec, '/');
        if (slash) {
            strncpy(scope, package_spec + 1, slash - package_spec - 1);
            scope[slash - package_spec - 1] = '\0';
            strncpy(name, slash + 1, sizeof(name)-1);
        } else {
            strcpy(name, package_spec + 1);
        }
    } else {
        strncpy(name, package_spec, sizeof(name)-1);
    }
    
    /* Vérifie si déjà installé */
    snprintf(target_dir, sizeof(target_dir), "/usr/local/lib/swiftvelox/%s", name);
    if (file_exists(target_dir)) {
        log_warn("Package déjà installé dans %s", target_dir);
        printf("Remplacer? (o/N): ");
        char response[4];
        fgets(response, sizeof(response), stdin);
        if (response[0] != 'o' && response[0] != 'O') {
            log_info("Installation annulée");
            return 0;
        }
        /* Supprime l'ancienne version */
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", target_dir);
        system(cmd);
    }
    
    /* Récupère les métadonnées */
    snprintf(url, sizeof(url), "%s/api/package/metadata/%s/%s",
             config.server_url, scope, name);
    
    buffer = download_url(url, config.token, 30);
    if (!buffer) {
        log_error("Package non trouvé ou erreur réseau");
        return 0;
    }
    
    /* Parse les métadonnées */
    json_t *meta = json_loads(buffer->data, 0, NULL);
    download_buffer_free(buffer);
    
    if (!meta) {
        log_error("Métadonnées invalides");
        return 0;
    }
    
    const char *version = json_string_value(json_object_get(meta, "version"));
    const char *sha256 = json_string_value(json_object_get(meta, "sha256"));
    
    if (!version) {
        log_error("Version non spécifiée dans les métadonnées");
        json_decref(meta);
        return 0;
    }
    
    /* Télécharge le package */
    snprintf(url, sizeof(url), "%s/package/download/%s/%s/%s",
             config.server_url, scope, name, version);
    
    log_info("Téléchargement de la version %s...", version);
    buffer = download_url(url, config.token, 60);
    
    if (!buffer || buffer->size == 0) {
        log_error("Échec du téléchargement");
        json_decref(meta);
        return 0;
    }
    
    /* Crée le répertoire cible */
    mkdir(target_dir, 0755);
    
    /* Traite le package */
    strncpy(pkg_info.name, name, sizeof(pkg_info.name)-1);
    strncpy(pkg_info.scope, scope, sizeof(pkg_info.scope)-1);
    strncpy(pkg_info.version, version, sizeof(pkg_info.version)-1);
    
    if (!process_zarch_package(buffer->data, buffer->size, target_dir, &pkg_info)) {
        log_error("Échec du traitement du package");
        download_buffer_free(buffer);
        json_decref(meta);
        return 0;
    }
    
    download_buffer_free(buffer);
    
    /* Vérifie le SHA256 si disponible */
    if (sha256) {
        char computed_sha[65];
        /* Note: nécessite le contenu original, pas le contenu traité */
        log_debug("SHA256 attendu: %s", sha256);
        /* Implémenter la vérification ici */
    }
    
    /* Sauvegarde les métadonnées locales */
    char meta_path[PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/.zarch_meta.json", target_dir);
    
    FILE *fp = fopen(meta_path, "w");
    if (fp) {
        json_dumpf(meta, fp, JSON_INDENT(2));
        fclose(fp);
    }
    
    json_decref(meta);
    
    log_success("Package %s v%s installé avec succès!", name, version);
    printf("Emplacement: %s\n", target_dir);
    
    return 1;
}

int cmd_publish(const char *path, const char *personal_code, int auto_version) {
    UserConfig config;
    char manifest_path[PATH_MAX];
    char archive_path[PATH_MAX];
    FILE *fp;
    json_t *manifest;
    json_error_t error;
    
    if (!load_config(&config)) {
        log_error("Non connecté. Utilisez 'zarch login' d'abord.");
        return 0;
    }
    
    if (!personal_code || !personal_code[0]) {
        log_error("Code personnel requis pour la publication");
        return 0;
    }
    
    /* Lit le manifeste */
    snprintf(manifest_path, sizeof(manifest_path), "%s/zarch.json", path);
    if (!file_exists(manifest_path)) {
        log_error("zarch.json manquant dans %s", path);
        return 0;
    }
    
    if (!(fp = fopen(manifest_path, "r"))) {
        log_error("Impossible de lire zarch.json: %s", strerror(errno));
        return 0;
    }
    
    manifest = json_loadf(fp, 0, &error);
    fclose(fp);
    
    if (!manifest) {
        log_error("zarch.json invalide (ligne %d): %s", error.line, error.text);
        return 0;
    }
    
    const char *name = json_string_value(json_object_get(manifest, "name"));
    const char *scope = json_string_value(json_object_get(manifest, "scope"));
    const char *version = json_string_value(json_object_get(manifest, "version"));
    const char *author = json_string_value(json_object_get(manifest, "author"));
    
    if (!name || !version) {
        log_error("Champs 'name' ou 'version' manquants");
        json_decref(manifest);
        return 0;
    }
    
    if (!scope) scope = "user";
    
    /* Incrémente la version si demandé */
    if (auto_version) {
        char new_version[32];
        int major, minor, patch;
        
        if (sscanf(version, "%d.%d.%d", &major, &minor, &patch) == 3) {
            patch++;
            snprintf(new_version, sizeof(new_version), "%d.%d.%d", major, minor, patch);
            json_object_set_new(manifest, "version", json_string(new_version));
            version = new_version;
            
            /* Sauvegarde la nouvelle version */
            fp = fopen(manifest_path, "w");
            if (fp) {
                json_dumpf(manifest, fp, JSON_INDENT(2));
                fclose(fp);
            }
            
            log_info("Version incrémentée: %s", version);
        }
    }
    
    /* Valide la structure du package */
    PackageInfo pkg_info = {0};
    snprintf(archive_path, sizeof(archive_path), "/tmp/%s-%s.tar.gz", name, version);
    
    /* Crée l'archive */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -czf \"%s\" -C \"%s\" . 2>/dev/null",
             archive_path, path);
    
    if (system(cmd) != 0) {
        log_error("Échec de la création de l'archive");
        json_decref(manifest);
        return 0;
    }
    
    if (!validate_svlib_structure(archive_path, &pkg_info)) {
        log_error("Structure .svlib invalide");
        remove(archive_path);
        json_decref(manifest);
        return 0;
    }
    
    /* Prépare l'upload */
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/package/upload/%s/%s?token=%s",
             config.server_url, scope, name, config.token);
    
    log_info("Publication de %s v%s...", name, version);
    
    /* Upload via curl (multipart form) */
    CURL *curl = curl_easy_init();
    if (!curl) {
        remove(archive_path);
        json_decref(manifest);
        return 0;
    }
    
    struct curl_httppost *form = NULL;
    struct curl_httppost *last = NULL;
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "file",
                 CURLFORM_FILE, archive_path,
                 CURLFORM_FILENAME, archive_path,
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "version",
                 CURLFORM_COPYCONTENTS, version,
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "personal_code",
                 CURLFORM_COPYCONTENTS, personal_code,
                 CURLFORM_END);
    
    curl_formadd(&form, &last,
                 CURLFORM_COPYNAME, "description",
                 CURLFORM_COPYCONTENTS, pkg_info.description,
                 CURLFORM_END);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
    
    DownloadBuffer resp = {0};
    resp.capacity = 4096;
    resp.data = malloc(resp.capacity);
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    curl_formfree(form);
    curl_easy_cleanup(curl);
    remove(archive_path);
    
    if (res != CURLE_OK) {
        log_error("Échec de l'upload: %s", curl_easy_strerror(res));
        free(resp.data);
        json_decref(manifest);
        return 0;
    }
    
    /* Parse la réponse */
    json_t *result = json_loads(resp.data, 0, NULL);
    free(resp.data);
    
    if (!result) {
        log_error("Réponse serveur invalide");
        json_decref(manifest);
        return 0;
    }
    
    const char *message = json_string_value(json_object_get(result, "message"));
    if (json_is_true(json_object_get(result, "success"))) {
        log_success("Publication réussie!");
        if (message) printf("%s\n", message);
    } else {
        const char *error_msg = json_string_value(json_object_get(result, "error"));
        log_error("Échec: %s", error_msg ? error_msg : "Raison inconnue");
        json_decref(result);
        json_decref(manifest);
        return 0;
    }
    
    json_decref(result);
    json_decref(manifest);
    return 1;
}

int cmd_search(const char *query) {
    UserConfig config;
    char url[512];
    DownloadBuffer *buffer;
    
    if (!load_config(&config)) {
        /* Recherche publique sans login */
        strcpy(config.server_url, "https://zenv-hub.onrender.com");
    }
    
    snprintf(url, sizeof(url), "%s/zarch/INDEX", config.server_url);
    
    log_info("Recherche de '%s'...", query ? query : "(tous)");
    
    buffer = download_url(url, NULL, 30);
    if (!buffer) {
        log_error("Impossible de récupérer l'index");
        return 0;
    }
    
    json_t *index = json_loads(buffer->data, 0, NULL);
    download_buffer_free(buffer);
    
    if (!index) {
        log_error("Index invalide");
        return 0;
    }
    
    json_t *packages = json_object_get(index, "packages");
    const char *key;
    json_t *value;
    int count = 0;
    
    printf("\n" COLOR_CYAN "📦 Paquets disponibles:\n" COLOR_RESET);
    printf("========================================\n");
    
    json_object_foreach(packages, key, value) {
        if (!query || strstr(key, query)) {
            const char *ver = json_string_value(json_object_get(value, "version"));
            const char *desc = json_string_value(json_object_get(value, "description"));
            const char *author = json_string_value(json_object_get(value, "author"));
            
            printf(COLOR_GREEN "• %s" COLOR_RESET " v%s\n", key, ver);
            if (desc) printf("  %s\n", desc);
            if (author) printf("  Par: %s\n", author);
            printf("\n");
            count++;
        }
    }
    
    printf("Trouvé: %d paquet(s)\n", count);
    json_decref(index);
    
    return count > 0;
}

int cmd_list(void) {
    char lib_path[] = "/usr/local/lib/swiftvelox";
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    
    if (!(dir = opendir(lib_path))) {
        log_error("Répertoire des librairies non trouvé: %s", lib_path);
        return 0;
    }
    
    printf("\n" COLOR_CYAN "📁 Paquets installés:\n" COLOR_RESET);
    printf("========================================\n");
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            char pkg_path[PATH_MAX];
            char meta_path[PATH_MAX];
            
            snprintf(pkg_path, sizeof(pkg_path), "%s/%s", lib_path, entry->d_name);
            snprintf(meta_path, sizeof(meta_path), "%s/.zarch_meta.json", pkg_path);
            
            if (file_exists(pkg_path)) {
                /* Essaie de lire les métadonnées */
                FILE *fp = fopen(meta_path, "r");
                if (fp) {
                    json_t *meta = json_loadf(fp, 0, NULL);
                    fclose(fp);
                    
                    if (meta) {
                        const char *version = json_string_value(json_object_get(meta, "version"));
                        const char *desc = json_string_value(json_object_get(meta, "description"));
                        
                        printf(COLOR_GREEN "• %s" COLOR_RESET, entry->d_name);
                        if (version) printf(" v%s", version);
                        printf("\n");
                        
                        if (desc) printf("  %s\n", desc);
                        
                        json_decref(meta);
                    }
                } else {
                    printf(COLOR_YELLOW "• %s" COLOR_RESET " (sans métadonnées)\n", entry->d_name);
                }
                
                count++;
            }
        }
    }
    
    closedir(dir);
    printf("\nTotal: %d paquet(s)\n", count);
    return count > 0;
}

/* ============================================================================
 * POINT D'ENTRÉE
 * ============================================================================ */

void print_usage(const char *progname) {
    printf("\n" COLOR_CYAN "🐧 Zarch Package Manager v%s\n\n" COLOR_RESET, VERSION);
    printf("Usage: %s <command> [options]\n\n", progname);
    printf("Commandes:\n");
    printf("  login <user> <pass>           Connexion au registre\n");
    printf("  logout                        Déconnexion\n");
    printf("  whoami                        Affiche l'utilisateur courant\n");
    printf("  init [path]                   Initialise un nouveau package\n");
    printf("  build [path]                  Construit le package\n");
    printf("  publish <path> <code>         Publie le package\n");
    printf("  install <package>             Installe un package\n");
    printf("  uninstall <package>           Désinstalle un package\n");
    printf("  search [query]                Recherche dans le registre\n");
    printf("  list                          Liste les packages installés\n");
    printf("  update                        Met à jour l'index local\n");
    printf("  version                       Affiche la version\n");
    printf("\nOptions pour 'publish':\n");
    printf("  --auto-version                Incrémente automatiquement la version\n");
    printf("  --force                       Force la publication\n");
    printf("\nExemples:\n");
    printf("  %s publish . CODE123 --auto-version\n", progname);
    printf("  %s install @math/calculus\n", progname);
    printf("  %s search \"json parser\"\n", progname);
}

int main(int argc, char **argv) {
    /* Initialisation globale */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    if (argc < 2) {
        print_usage(argv[0]);
        curl_global_cleanup();
        return 1;
    }
    
    const char *command = argv[1];
    int result = 0;
    
    /* Bannière */
    if (argc > 1) {
        printf(COLOR_CYAN "\n🐧 Zarch Package Manager v%s\n" COLOR_RESET, VERSION);
    }
    
    /* Dispatch des commandes */
    if (strcmp(command, "login") == 0 && argc >= 4) {
        result = cmd_login(argv[2], argv[3]);
    } else if (strcmp(command, "install") == 0 && argc >= 3) {
        result = cmd_install(argv[2]);
    } else if (strcmp(command, "publish") == 0 && argc >= 4) {
        int auto_version = 0;
        if (argc >= 5 && strcmp(argv[4], "--auto-version") == 0) {
            auto_version = 1;
        }
        result = cmd_publish(argv[2], argv[3], auto_version);
    } else if (strcmp(command, "search") == 0) {
        result = cmd_search(argc >= 3 ? argv[2] : NULL);
    } else if (strcmp(command, "list") == 0) {
        result = cmd_list();
    } else if (strcmp(command, "version") == 0) {
        printf("Version %s\n", VERSION);
        result = 1;
    } else if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0) {
        print_usage(argv[0]);
        result = 1;
    } else {
        log_error("Commande non reconnue: %s", command);
        print_usage(argv[0]);
        result = 0;
    }
    
    curl_global_cleanup();
    return result ? 0 : 1;
}
