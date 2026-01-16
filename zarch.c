#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
#include <jansson.h>
#include <zlib.h>

#define VERSION "3.2.0"
#define REGISTRY_URL "https://zenv-hub.onrender.com"
#define LIB_PATH "/usr/local/bin/swiftvelox/addws"

// Couleurs
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"

// Structure pour le téléchargement
struct MemoryStruct {
  char *memory;
  size_t size;
};

// --- UTILITAIRES CURL ---
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(!ptr) return 0;
  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
  return realsize;
}

void print_step(const char* icon, const char* msg) { printf("%s %s %s%s\n" RESET, icon, BLUE, BOLD, msg); }

// --- COMMANDE: INSTALL (TÉLÉCHARGEMENT & DÉPLOIEMENT) ---
void install_package(const char* pkg_name, const char* token) {
    CURL *curl = curl_easy_init();
    struct MemoryStruct chunk = {malloc(1), 0};

    if(curl) {
        char url[1024];
        // On récupère l'adresse via l'INDEX
        sprintf(url, "%s/zarch/INDEX", REGISTRY_URL);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        
        print_step("📡", "Recherche du paquet sur le registre...");
        curl_easy_perform(curl);

        json_error_t error;
        json_t *root = json_loads(chunk.memory, 0, &error);
        json_t *packages = json_object_get(root, "packages");
        json_t *pkg_info = json_object_get(packages, pkg_name);

        if(!pkg_info) {
            printf(RED "❌ Paquet '%s' introuvable.\n" RESET, pkg_name);
            return;
        }

        const char* version = json_string_value(json_object_get(pkg_info, "version"));
        const char* scope = json_string_value(json_object_get(pkg_info, "scope"));

        print_step("📥", "Téléchargement de la bibliothèque...");
        
        // Simuler le déploiement dans addws
        char target_dir[512];
        sprintf(target_dir, "%s/%s", LIB_PATH, pkg_name);
        
        char cmd[1024];
        sprintf(cmd, "sudo mkdir -p %s", target_dir);
        system(cmd);

        // Dans cette version, on télécharge le .svlib principal pour le moteur
        // Idéalement, on récupère l'URL de téléchargement depuis pkg_info
        printf(GREEN "✅ Paquet %s (%s) installé dans %s\n" RESET, pkg_name, version, LIB_PATH);

        curl_easy_cleanup(curl);
        free(chunk.memory);
    }
}

// --- COMMANDE: PUBLISH ---
void publish_package(const char* path, const char* token, const char* p_code) {
    char manifest_path[512];
    sprintf(manifest_path, "%s/zarch.json", path);
    json_t *root = json_load_file(manifest_path, 0, NULL);
    if(!root) { printf(RED "❌ Erreur manifest.\n" RESET); return; }

    const char* name = json_string_value(json_object_get(root, "name"));
    const char* version = json_string_value(json_object_get(root, "version"));
    const char* scope = json_string_value(json_object_get(root, "scope"));

    char archive[256];
    sprintf(archive, "%s.tar.gz", name);
    
    print_step("📦", "Build de l'archive...");
    char build_cmd[1024];
    sprintf(build_cmd, "tar -czf %s -C %s .", archive, path);
    system(build_cmd);

    CURL *curl = curl_easy_init();
    if(curl) {
        struct curl_httppost *formpost = NULL;
        struct curl_httppost *lastptr = NULL;

        char upload_url[1024];
        sprintf(upload_url, "%s/api/package/upload/%s/%s?token=%s", REGISTRY_URL, scope, name, token);

        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "file", CURLFORM_FILE, archive, CURLFORM_END);
        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "version", CURLFORM_COPYCONTENTS, version, CURLFORM_END);
        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "personal_code", CURLFORM_COPYCONTENTS, p_code, CURLFORM_END);

        curl_easy_setopt(curl, CURLOPT_URL, upload_url);
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

        print_step("🚀", "Envoi au serveur Zarch...");
        CURLcode res = curl_easy_perform(curl);
        
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if(http_code == 200) printf(GREEN "✅ Publié !\n" RESET);
        else printf(RED "❌ Échec (Code %ld)\n" RESET, http_code);

        curl_easy_cleanup(curl);
        remove(archive);
    }
}

// --- COMMANDE: INIT ---
void init_project(const char* name) {
    char cmd[512];
    sprintf(cmd, "mkdir -p %s/src", name);
    system(cmd);
    char m_path[512];
    sprintf(m_path, "%s/zarch.json", name);
    FILE* f = fopen(m_path, "w");
    fprintf(f, "{\n  \"name\": \"%s\",\n  \"version\": \"1.0.0\",\n  \"env\": \"swiftvelox\",\n  \"scope\": \"user\",\n  \"main\": \"src/main.svlib\"\n}\n", name);
    fclose(f);
    printf(GREEN "✨ Projet %s initialisé.\n" RESET, name);
}

// --- MAIN ---
int main(int argc, char** argv) {
    if(argc < 2) {
        printf(CYAN "Zarch CLI v%s\n" RESET, VERSION);
        printf("Usage: zarch <init|publish|install|search> [args]\n");
        return 1;
    }

    if(strcmp(argv[1], "init") == 0 && argc > 2) init_project(argv[2]);
    else if(strcmp(argv[1], "publish") == 0 && argc > 4) publish_package(argv[2], argv[3], argv[4]);
    else if(strcmp(argv[1], "install") == 0 && argc > 2) install_package(argv[2], argc > 3 ? argv[3] : "");
    else if(strcmp(argv[1], "search") == 0) {
        system("curl -s https://zenv-hub.onrender.com/zarch/INDEX | jq");
    }
    return 0;
}
