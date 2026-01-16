#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>

#define VERSION "4.2.0"
#define REGISTRY_URL "https://zenv-hub.onrender.com"
#define LIB_PATH "/usr/local/bin/swiftvelox/addws"

#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define BLUE    "\033[34m"

struct MemoryStruct { char *memory; size_t size; };

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

void print_step(const char* icon, const char* msg) {
    printf("%s %s%s%s\n" RESET, icon, BLUE, BOLD, msg);
}

// --- BUILD ---
int build_package(const char* path, char* archive_out) {
    char manifest[512]; sprintf(manifest, "%s/zarch.json", path);
    json_t *root = json_load_file(manifest, 0, NULL);
    if(!root) { fprintf(stderr, RED "❌ Aucun manifest trouvé.\n" RESET); return 1; }

    const char* name = json_string_value(json_object_get(root, "name"));
    const char* version = json_string_value(json_object_get(root, "version"));
    sprintf(archive_out, "%s/%s.%s.tar.gz", path, name, version);

    print_step("📦", "Build de l'archive versionnée...");
    char cmd[1024]; sprintf(cmd, "tar -czf %s -C %s .", archive_out, path);
    return system(cmd);
}

// --- PUBLISH ---
void publish_package(const char* path, const char* token, const char* p_code) {
    char archive[512];
    if (build_package(path, archive) != 0) return;

    CURL *curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");

        char manifest[512]; sprintf(manifest, "%s/zarch.json", path);
        json_t *root = json_load_file(manifest, 0, NULL);
        const char* name = json_string_value(json_object_get(root, "name"));
        const char* scope = json_string_value(json_object_get(root, "scope"));

        char url[1024];
        sprintf(url, "%s/api/package/upload/%s/%s?token=%s", REGISTRY_URL, scope, name, token);

        struct curl_httppost *form = NULL;
        struct curl_httppost *last = NULL;
        curl_formadd(&form, &last, CURLFORM_COPYNAME, "file", CURLFORM_FILE, archive, CURLFORM_END);
        curl_formadd(&form, &last, CURLFORM_COPYNAME, "personal_code", CURLFORM_COPYCONTENTS, p_code, CURLFORM_END);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        print_step("🚀", "Upload vers le registre...");
        if (curl_easy_perform(curl) == CURLE_OK) print_step("✅", "Publication réussie !");
        
        curl_easy_cleanup(curl);
        remove(archive);
    }
}

// --- INSTALL ---
void install_package(const char* pkg_name) {
    CURL *curl = curl_easy_init();
    struct MemoryStruct chunk = {malloc(1), 0};
    if (curl) {
        char url[512]; sprintf(url, "%s/zarch/INDEX", REGISTRY_URL);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_perform(curl);

        json_t *index = json_loads(chunk.memory, 0, NULL);
        json_t *pkg = json_object_get(json_object_get(index, "packages"), pkg_name);
        if (!pkg) { printf(RED "❌ Introuvable.\n" RESET); return; }

        const char* scope = json_string_value(json_object_get(pkg, "scope"));
        const char* version = json_string_value(json_object_get(pkg, "version"));

        char target[512]; sprintf(target, "%s/%s", LIB_PATH, pkg_name);
        char cmd_dir[512]; sprintf(cmd_dir, "mkdir -p %s", target); system(cmd_dir);

        char dl_url[1024];
        sprintf(dl_url, "%s/package/download/%s/%s/%s", REGISTRY_URL, scope, pkg_name, version);
        char archive_path[512]; sprintf(archive_path, "%s/%s.%s.tar.gz", target, pkg_name, version);

        char cmd_dl[1024]; sprintf(cmd_dl, "curl -L -s -o %s %s", archive_path, dl_url);
        if (system(cmd_dl) == 0) {
            char cmd_extract[1024]; sprintf(cmd_extract, "tar -xzf %s -C %s", archive_path, target);
            if (system(cmd_extract) == 0) printf(GREEN "✅ Installé dans %s\n" RESET, target);
            remove(archive_path);
        }
        curl_easy_cleanup(curl);
    }
}

// --- SEARCH ---
void search_registry() {
    print_step("🔍", "Lecture de l'INDEX distant...");
    CURL *curl = curl_easy_init();
    struct MemoryStruct chunk = {malloc(1), 0};
    if (curl) {
        char url[512]; sprintf(url, "%s/zarch/INDEX", REGISTRY_URL);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_perform(curl);

        json_t *index = json_loads(chunk.memory, 0, NULL);
        json_t *packages = json_object_get(index, "packages");
        const char* key; json_t *value;
        void *iter = json_object_iter(packages);
        while(iter) {
            key = json_object_iter_key(iter);
            value = json_object_iter_value(iter);
            printf("📦 %s v%s (scope: %s)\n", key,
                   json_string_value(json_object_get(value, "version")),
                   json_string_value(json_object_get(value, "scope")));
            iter = json_object_iter_next(packages, iter);
        }
        curl_easy_cleanup(curl);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Zarch CLI v%s\nUsage: zarch <build|publish|install|search>\n", VERSION);
        return 1;
    }
    if (strcmp(argv[1], "build") == 0) { char a[512]; build_package("./math", a); }
    else if (strcmp(argv[1], "publish") == 0) publish_package("./math", argv[2], argv[3]);
    else if (strcmp(argv[1], "install") == 0) install_package(argv[2]);
    else if (strcmp(argv[1], "search") == 0) search_registry();
    return 0;
}
