#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
#include <jansson.h>

#define VERSION "3.1.0"
#define REGISTRY_URL "https://zenv-hub.onrender.com"
#define LIB_PATH "/usr/local/bin/swiftvelox/addws"

// Couleurs Terminal
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"

// --- UTILITAIRES ---
void print_step(const char* icon, const char* msg) { printf("%s %s %s%s\n" RESET, icon, BLUE, BOLD, msg); }
void print_err(const char* msg) { printf(RED "❌ Erreur: %s\n" RESET, msg); }
void print_ok(const char* msg) { printf(GREEN "✅ %s\n" RESET, msg); }

// --- COMMANDE: BUILD ---
int build_package(const char* path, char* out_archive) {
    char manifest_path[512];
    sprintf(manifest_path, "%s/zarch.json", path);
    
    json_error_t error;
    json_t *root = json_load_file(manifest_path, 0, &error);
    if(!root) { print_err("zarch.json introuvable ou malformé."); return -1; }

    const char* name = json_string_value(json_object_get(root, "name"));
    sprintf(out_archive, "%s.tar.gz", name);

    print_step("📦", "Compression du package SwiftVelox...");
    char cmd[1024];
    // Compresse le contenu du dossier (incluant src/)
    sprintf(cmd, "tar -czf %s -C %s .", out_archive, path);
    
    if(system(cmd) == 0) {
        print_ok("Archive créée avec succès.");
        return 0;
    }
    return -1;
}

// --- COMMANDE: LOGIN ---
void login_user(const char* user, const char* pass) {
    CURL *curl = curl_easy_init();
    if(curl) {
        char url[512];
        sprintf(url, "%s/api/auth/login", REGISTRY_URL);
        
        json_t *login_data = json_object();
        json_object_set_new(login_data, "username", json_string(user));
        json_object_set_new(login_data, "password", json_string(pass));
        char *json_str = json_dumps(login_data, 0);

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        print_step("🔑", "Identification auprès du registre...");
        CURLcode res = curl_easy_perform(curl);
        if(res == CURLE_OK) printf("\n" GREEN "Connecté ! Copiez votre token depuis la réponse ci-dessus." RESET "\n");
        
        curl_easy_cleanup(curl);
        free(json_str);
    }
}

// --- COMMANDE: PUBLISH ---
void publish_package(const char* path, const char* token, const char* p_code) {
    char archive[256];
    if(build_package(path, archive) != 0) return;

    json_t *root = json_load_file("zarch.json", 0, NULL);
    const char* name = json_string_value(json_object_get(root, "name"));
    const char* version = json_string_value(json_object_get(root, "version"));
    const char* scope = json_string_value(json_object_get(root, "scope"));

    CURL *curl = curl_easy_init();
    if(curl) {
        struct curl_httppost *formpost = NULL;
        struct curl_httppost *lastptr = NULL;

        char upload_url[1024];
        // On passe le token en paramètre d'URL pour contourner les problèmes de session/redirect
        sprintf(upload_url, "%s/api/package/upload/%s/%s?token=%s", REGISTRY_URL, scope, name, token);

        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "file", CURLFORM_FILE, archive, CURLFORM_END);
        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "version", CURLFORM_COPYCONTENTS, version, CURLFORM_END);
        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "personal_code", CURLFORM_COPYCONTENTS, p_code, CURLFORM_END);

        curl_easy_setopt(curl, CURLOPT_URL, upload_url);
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); // Ne pas suivre la redirection /login si erreur

        print_step("🚀", "Publication sur Zarch Registry...");
        CURLcode res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if(http_code == 200) print_ok("Paquet publié et indexé !");
        else if(http_code == 302) print_err("Authentification échouée (Token invalide).");
        else printf(RED "❌ Erreur serveur (Code %ld)\n" RESET, http_code);

        curl_easy_cleanup(curl);
        remove(archive);
    }
}

// --- COMMANDE: SEARCH ---
void search_packages() {
    CURL *curl = curl_easy_init();
    if(curl) {
        char url[512];
        sprintf(url, "%s/zarch/INDEX", REGISTRY_URL);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        
        print_step("🔍", "Exploration du catalogue Zarch...");
        printf(CYAN "--------------------------------------------------\n");
        curl_easy_perform(curl);
        printf("\n--------------------------------------------------\n" RESET);
        curl_easy_cleanup(curl);
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

    char s_path[512];
    sprintf(s_path, "%s/src/main.svlib", name);
    FILE* s = fopen(s_path, "w");
    fprintf(s, "// Module SwiftVelox: %s\nfn hello() {\n    print(\"Salut depuis %s !\");\n}\n", name, name);
    fclose(s);

    print_ok("Structure SwiftVelox créée (src/ + zarch.json).");
}

// --- MAIN ---
int main(int argc, char** argv) {
    if(argc < 2) {
        printf(CYAN BOLD "Zarch CLI v%s - SwiftVelox Package Manager\n" RESET, VERSION);
        printf("Usage:\n");
        printf("  zarch init <nom>             Créer un projet\n");
        printf("  zarch build <chemin>         Créer l'archive .tar.gz\n");
        printf("  zarch login <user> <pass>    Se connecter\n");
        printf("  zarch publish <path> <tok> <p_code>  Publier\n");
        printf("  zarch search                 Lister les paquets\n");
        return 1;
    }

    if(strcmp(argv[1], "init") == 0 && argc > 2) init_project(argv[2]);
    else if(strcmp(argv[1], "build") == 0 && argc > 2) { char a[256]; build_package(argv[2], a); }
    else if(strcmp(argv[1], "login") == 0 && argc > 3) login_user(argv[2], argv[3]);
    else if(strcmp(argv[1], "publish") == 0 && argc > 4) publish_package(argv[2], argv[3], argv[4]);
    else if(strcmp(argv[1], "search") == 0) search_packages();
    else print_err("Commande inconnue ou arguments manquants.");

    return 0;
}
