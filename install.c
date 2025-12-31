#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    printf("🔧 Installing zarch (C version)...\n");
    
    // 1. Installer gcc si pas présent
    if (system("which gcc > /dev/null 2>&1") != 0) {
        printf("📦 Installing gcc...\n");
        system("sudo apk add build-base");
    }
    
    // 2. Télécharger le code source
    printf("📥 Downloading source code...\n");
    system("curl -s -o /tmp/zarch.c https://raw.githubusercontent.com/gopu-inc/lib/main/zarch.c");
    system("curl -s -o /tmp/Makefile https://raw.githubusercontent.com/gopu-inc/lib/main/Makefile");
    
    // 3. Compiler
    printf("⚙️  Compiling...\n");
    chdir("/tmp");
    if (system("gcc -Wall -O2 -o zarch zarch.c") != 0) {
        printf("❌ Compilation failed\n");
        return 1;
    }
    
    // 4. Installer
    printf("📦 Installing...\n");
    system("sudo cp zarch /usr/local/bin/");
    system("sudo chmod +x /usr/local/bin/zarch");
    
    // 5. Configurer PATH
    printf("🔧 Configuring PATH...\n");
    FILE *bashrc = fopen(getenv("HOME"), "a");
    if (bashrc) {
        fprintf(bashrc, "\nexport PATH=\"/usr/local/bin:$PATH\"\n");
        fclose(bashrc);
    }
    
    printf("\n✅ zarch installed successfully!\n");
    printf("📁 Location: /usr/local/bin/zarch\n");
    printf("📖 Usage: zarch list\n");
    
    // Nettoyer
    system("rm -f /tmp/zarch.c /tmp/Makefile /tmp/zarch");
    
    return 0;
}
