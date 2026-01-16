#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <jansson.h>
#include <zlib.h>

#define VERSION "3.0.0"
#define REGISTRY_URL "https://zenv-hub.onrender.com"
#define LIB_PATH "/usr/local/bin/swiftvelox/addws"

// Couleurs
#define RESET "\033[0m"
#define BOLD  "\033[1m"
#define RED   "\033[31m"
#define GREEN "\033[32m"
#define BLUE  "\033[34m"

void print_step(char* msg) { printf(BLUE "==>" RESET BOLD " %s\n" RESET, msg); }

// --- LOGIQUE DE COMPRESSION ---
int compress_folder(const char* source_dir, const char* out_name) {
    char cmd[512];
    // On compresse tout le contenu, y compris src/ et le manifest
    sprintf(cmd, "tar -czf %s -C %s .", out_name, source_dir);
    return system(cmd);
}

// --- LOGIQUE PUBLISH (Communication avec ton serveur Flask) ---
void publish_package(const char* path, const char* token, const char* p_code) {
    json_t *root;
    json_error_t error;
    char manifest_path[256];
    sprintf(manifest_path, "%s/zarch.json", path);
    
    root = json_load_file(manifest_path, 0, &error);
    if(!root) { printf(RED "Erreur: zarch.json introuvable.\n" RESET); return; }

    const char* name = json_string_value(json_object_get(root, "name"));
    const char* version = json_string_value(json_object_get(root, "version"));
    const char* scope = json_string_value(json_object_get(root, "scope"));

    print_step("Préparation du paquet Zarch...");
    char archive_name[256];
    sprintf(archive_name, "%s.tar.gz", name);
    compress_folder(path, archive_name);

    CURL *curl = curl_easy_init();
    if(curl) {
        struct curl_httppost *formpost = NULL;
        struct curl_httppost *lastptr = NULL;

        char upload_url[512];
        sprintf(upload_url, "%s/api/package/upload/%s/%s", REGISTRY_URL, scope ? scope : "user", name);

        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "file",
                     CURLFORM_FILE, archive_name, CURLFORM_END);
        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "version",
                     CURLFORM_COPYCONTENTS, version, CURLFORM_END);
        curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "personal_code",
                     CURLFORM_COPYCONTENTS, p_code, CURLFORM_END);

        struct curl_slist *headers = NULL;
        char auth_header[512];
        sprintf(auth_header, "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, auth_header);

        curl_easy_setopt(curl, CURLOPT_URL, upload_url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

        print_step("Upload vers le registre sécurisé...");
        CURLcode res = curl_easy_perform(curl);
        if(res == CURLE_OK) printf(GREEN "✅ Succès ! Paquet %s publié.\n" RESET, name);
        else printf(RED "❌ Échec de l'upload.\n" RESET);

        curl_easy_cleanup(curl);
        remove(archive_name);
    }
}

// --- LOGIQUE INITIALISATION ---
void init_project(const char* name) {
    mkdir(name, 0755);
    char src_path[256];
    sprintf(src_path, "%s/src", name);
    mkdir(src_path, 0755);

    // Création du manifest SwiftVelox
    char m_path[256];
    sprintf(m_path, "%s/zarch.json", name);
    FILE* f = fopen(m_path, "w");
    fprintf(f, "{\n  \"name\": \"%s\",\n  \"version\": \"1.0.0\",\n  \"env\": \"swiftvelox\",\n  \"scope\": \"user\",\n  \"main\": \"src/main.svlib\"\n}\n", name);
    fclose(f);

    // Création du fichier source d'exemple
    char s_path[256];
    sprintf(s_path, "%s/src/main.svlib", name);
    FILE* s = fopen(s_path, "w");
    fprintf(s, "fn hello() {\n    print(\"Hello depuis le module %s !\");\n}\n", name);
    fclose(s);

    printf(GREEN "✨ Projet SwiftVelox '%s' prêt avec dossier src/\n" RESET, name);
}

int main(int argc, char** argv) {
    if(argc < 2) {
        printf("Zarch CLI v%s - SwiftVelox Only\n", VERSION);
        printf("Usage: zarch <init|publish|install> [args]\n");
        return 1;
    }

    if(strcmp(argv[1], "init") == 0) {
        init_project(argc > 2 ? argv[2] : "new_package");
    } 
    else if(strcmp(argv[1], "publish") == 0) {
        if(argc < 5) { printf("Usage: zarch publish <path> <token> <personal_code>\n"); return 1; }
        publish_package(argv[2], argv[3], argv[4]);
    }
    return 0;
}
