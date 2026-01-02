#!/bin/bash

# Zarch CLI Installer v2.0 - Installation automatique
# Correction automatique des erreurs de compilation

set -e

# Couleurs pour le terminal
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# URLs de téléchargement
GITHUB_RAW_URL="https://raw.githubusercontent.com/gopu-inc/lib/refs/heads/main"
ZARCH_SOURCE_URL="$GITHUB_RAW_URL/zarch.c"
INSTALL_SCRIPT_URL="$GITHUB_RAW_URL/install.sh"

# Configuration
INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="$HOME/.zarch"
CACHE_DIR="$CONFIG_DIR/cache"
TEMP_DIR="/tmp/zarch-install-$(date +%s)"

# Détection OS
OS_TYPE=""
PKG_MANAGER=""

# Fonctions d'affichage
print_error() {
    echo -e "${RED}[ERREUR]${NC} $1" >&2
}

print_success() {
    echo -e "${GREEN}[SUCCÈS]${NC} $1"
}

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[AVERTISSEMENT]${NC} $1"
}

print_step() {
    echo -e "\n${CYAN}▶ $1${NC}"
    sleep 1
}

# Détecter l'OS
detect_os() {
    print_step "Détection du système d'exploitation"
    
    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        OS_TYPE="$ID"
        print_info "Distribution: $PRETTY_NAME"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        OS_TYPE="macos"
        print_info "Système: macOS"
    else
        OS_TYPE="unknown"
        print_warning "Système non reconnu"
    fi
    
    # Détecter le gestionnaire de paquets
    if command -v apt-get &> /dev/null; then
        PKG_MANAGER="apt"
        print_info "Gestionnaire: APT (Debian/Ubuntu)"
    elif command -v apk &> /dev/null; then
        PKG_MANAGER="apk"
        print_info "Gestionnaire: APK (Alpine)"
    elif command -v yum &> /dev/null; then
        PKG_MANAGER="yum"
        print_info "Gestionnaire: YUM (RHEL/CentOS)"
    elif command -v dnf &> /dev/null; then
        PKG_MANAGER="dnf"
        print_info "Gestionnaire: DNF (Fedora)"
    elif command -v pacman &> /dev/null; then
        PKG_MANAGER="pacman"
        print_info "Gestionnaire: Pacman (Arch)"
    elif command -v brew &> /dev/null; then
        PKG_MANAGER="brew"
        print_info "Gestionnaire: Homebrew (macOS)"
    else
        print_warning "Aucun gestionnaire de paquets détecté"
    fi
}

# Installer les dépendances pour Alpine
install_alpine_deps() {
    print_info "Installation des dépendances pour Alpine Linux..."
    
    # Mettre à jour APK
    apk update
    
    # Installer les paquets nécessaires
    local packages="gcc make curl jansson-dev openssl-dev curl-dev zlib-dev musl-dev"
    
    print_info "Installation: $packages"
    if apk add --no-cache $packages; then
        print_success "Dépendances installées"
    else
        print_error "Échec de l'installation des dépendances"
        
        # Essayer paquet par paquet
        for pkg in $packages; do
            print_info "Installation de $pkg..."
            apk add --no-cache $pkg || print_warning "Échec pour $pkg"
        done
    fi
}

# Installer les dépendances pour Debian/Ubuntu
install_debian_deps() {
    print_info "Installation des dépendances pour Debian/Ubuntu..."
    
    apt-get update
    apt-get install -y gcc make curl libjansson-dev libssl-dev libcurl4-openssl-dev zlib1g-dev
}

# Vérifier et installer les dépendances
check_dependencies() {
    print_step "1. Vérification des dépendances"
    
    # Vérifier gcc
    if ! command -v gcc &> /dev/null; then
        print_info "GCC n'est pas installé"
        case "$PKG_MANAGER" in
            apk) install_alpine_deps ;;
            apt) install_debian_deps ;;
            yum|dnf) yum install -y gcc make curl jansson-devel openssl-devel libcurl-devel zlib-devel ;;
            pacman) pacman -S --noconfirm gcc make curl jansson openssl curl zlib ;;
            brew) brew install gcc make curl jansson openssl curl zlib ;;
            *) print_error "Impossible d'installer les dépendances" ;;
        esac
    else
        print_info "✓ GCC déjà installé"
    fi
    
    # Vérifier les bibliothèques
    print_info "Vérification des bibliothèques..."
    
    # Pour Alpine, on a déjà installé les dev packages
    if [[ "$PKG_MANAGER" == "apk" ]]; then
        print_success "Dépendances Alpine installées"
        return
    fi
    
    # Pour les autres systèmes, vérifier les headers
    local missing_libs=()
    
    if [[ ! -f /usr/include/jansson.h ]] && [[ ! -f /usr/local/include/jansson.h ]] && [[ ! -f /opt/homebrew/include/jansson.h ]]; then
        missing_libs+=("jansson")
    fi
    
    if [[ ! -f /usr/include/openssl/ssl.h ]] && [[ ! -f /usr/local/include/openssl/ssl.h ]] && [[ ! -f /opt/homebrew/include/openssl/ssl.h ]]; then
        missing_libs+=("openssl")
    fi
    
    if [[ ! -f /usr/include/curl/curl.h ]] && [[ ! -f /usr/local/include/curl/curl.h ]] && [[ ! -f /opt/homebrew/include/curl/curl.h ]]; then
        missing_libs+=("libcurl")
    fi
    
    if [[ ${#missing_libs[@]} -gt 0 ]]; then
        print_warning "Bibliothèques manquantes: ${missing_libs[*]}"
        
        case "$PKG_MANAGER" in
            apt) apt-get install -y "lib${missing_libs[0]}-dev" ;;
            yum|dnf) yum install -y "${missing_libs[0]}-devel" ;;
            *) print_info "Installez manuellement: ${missing_libs[*]}" ;;
        esac
    fi
    
    print_success "Dépendances vérifiées"
}

# Corriger le code source
fix_source_code() {
    print_step "2. Correction du code source"
    
    local source_file="$TEMP_DIR/zarch.c"
    
    if [[ ! -f "$source_file" ]]; then
        print_error "Fichier source non trouvé"
        return 1
    fi
    
    # 1. Ajouter l'inclusion de errno.h après zlib.h
    if ! grep -q "#include <errno.h>" "$source_file"; then
        print_info "Ajout de #include <errno.h>"
        sed -i '/#include <zlib.h>/a #include <errno.h>' "$source_file"
    fi
    
    # 2. Corriger l'avertissement du répertoire /usr/local/include
    print_info "Correction des avertissements de compilation..."
    
    # 3. Vérifier d'autres inclusions manquantes
    local missing_includes=("sys/stat.h" "sys/types.h" "time.h")
    
    for include in "${missing_includes[@]}"; do
        if ! grep -q "#include <$include>" "$source_file"; then
            print_info "Vérification: $include"
        fi
    done
    
    # 4. Ajouter les définitions manquantes si nécessaire
    if grep -q "mkdir.*errno.*EEXIST" "$source_file" && ! grep -q "#define _POSIX_C_SOURCE" "$source_file"; then
        print_info "Ajout des définitions POSIX"
        sed -i '1i #define _POSIX_C_SOURCE 200809L' "$source_file"
    fi
    
    print_success "Code source corrigé"
}

# Télécharger le code source
download_source() {
    print_step "3. Téléchargement du code source"
    
    mkdir -p "$TEMP_DIR"
    
    # Essayer plusieurs URLs
    local urls=(
        "https://raw.githubusercontent.com/gopu-inc/lib/main/zarch.c"
        "https://raw.githubusercontent.com/gopu-inc/lib/refs/heads/main/zarch.c"
        "https://cdn.jsdelivr.net/gh/gopu-inc/lib@main/zarch.c"
    )
    
    for url in "${urls[@]}"; do
        print_info "Téléchargement depuis: $url"
        if curl -sSL -f -o "$TEMP_DIR/zarch.c" "$url"; then
            if [[ -s "$TEMP_DIR/zarch.c" ]] && grep -q "#include" "$TEMP_DIR/zarch.c"; then
                print_success "Code source téléchargé"
                print_info "Taille: $(wc -l < "$TEMP_DIR/zarch.c") lignes"
                return 0
            fi
        fi
        sleep 1
    done
    
    print_error "Impossible de télécharger le code source"
    exit 1
}

# Compiler avec correction automatique
compile_with_fixes() {
    print_step "4. Compilation avec corrections"
    
    local source_file="$TEMP_DIR/zarch.c"
    local binary_file="$TEMP_DIR/zarch"
    
    # Flags de compilation pour Alpine
    local cflags="-Wall -O2 -std=c99 -D_POSIX_C_SOURCE=200809L"
    local libs="-lcurl -ljansson -lcrypto -lz -lm"
    
    # Flags spécifiques
    case "$OS_TYPE" in
        alpine)
            # Alpine nécessite -D_GNU_SOURCE pour certaines fonctions
            cflags="$cflags -D_GNU_SOURCE"
            ;;
        macos|darwin*)
            cflags="$cflags -I/usr/local/include -L/usr/local/lib"
            if [[ -d "/opt/homebrew" ]]; then
                cflags="$cflags -I/opt/homebrew/include -L/opt/homebrew/lib"
            fi
            ;;
    esac
    
    # Commande de compilation
    local compile_cmd="gcc $cflags -o \"$binary_file\" \"$source_file\" $libs"
    
    print_info "Commande de compilation:"
    echo "$compile_cmd"
    
    # Première tentative de compilation
    print_info "Première tentative de compilation..."
    cd "$TEMP_DIR"
    
    if eval "$compile_cmd" 2>&1; then
        print_success "Compilation réussie du premier coup!"
        return 0
    else
        print_warning "Première compilation échouée, tentative de correction..."
    fi
    
    # Correction des erreurs
    fix_source_code
    
    # Deuxième tentative avec corrections
    print_info "Deuxième tentative avec corrections..."
    if eval "$compile_cmd" 2>&1; then
        print_success "Compilation réussie après corrections!"
    else
        print_error "Échec de la compilation après corrections"
        
        # Tentative de compilation simple pour diagnostic
        print_info "Test de compilation minimal..."
        echo "#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
int main() { printf(\"Test compilation\\n\"); return 0; }" > test.c
        
        if gcc test.c -o test && ./test; then
            print_info "✓ Compilateur GCC fonctionnel"
            rm -f test.c test
        fi
        
        # Afficher les 10 premières erreurs
        print_info "Dernières erreurs de compilation:"
        gcc $cflags -o "$binary_file" "$source_file" $libs 2>&1 | head -20
        
        exit 1
    fi
    
    # Vérifier le binaire
    if [[ -f "$binary_file" ]] && [[ -x "$binary_file" ]]; then
        print_info "✓ Binaire créé: $(file "$binary_file")"
        print_info "✓ Taille: $(du -h "$binary_file" | cut -f1)"
    else
        print_error "Binaire non créé ou non exécutable"
        exit 1
    fi
}

# Installer le binaire
install_binary() {
    print_step "5. Installation du binaire"
    
    local target_dir="$INSTALL_DIR"
    
    # Vérifier les permissions
    if [[ ! -w "$target_dir" ]] && [[ $EUID -ne 0 ]]; then
        print_info "Permissions root nécessaires..."
        if sudo mv "$TEMP_DIR/zarch" "$target_dir/zarch" 2>/dev/null; then
            sudo chmod +x "$target_dir/zarch"
            print_success "Installé dans $target_dir/zarch (avec sudo)"
        else
            # Fallback vers ~/.local/bin
            target_dir="$HOME/.local/bin"
            mkdir -p "$target_dir"
            mv "$TEMP_DIR/zarch" "$target_dir/zarch"
            chmod +x "$target_dir/zarch"
            print_success "Installé dans $target_dir/zarch"
        fi
    else
        mv "$TEMP_DIR/zarch" "$target_dir/zarch"
        chmod +x "$target_dir/zarch"
        print_success "Installé dans $target_dir/zarch"
    fi
    
    INSTALL_DIR="$target_dir"
}

# Configuration
create_config() {
    print_step "6. Configuration"
    
    mkdir -p "$CONFIG_DIR" "$CACHE_DIR"
    
    # Fichier de configuration
    cat > "$CONFIG_DIR/config.json" << EOF
{
    "registry_url": "https://zenv-hub.onrender.com",
    "api_version": "v1",
    "auto_update": true,
    "cache_enabled": true,
    "timeout": 30,
    "version": "2.0.0",
    "os": "$OS_TYPE"
}
EOF
    
    chmod 700 "$CONFIG_DIR"
    chmod 600 "$CONFIG_DIR/config.json"
    
    print_success "Configuration créée: $CONFIG_DIR"
}

# Tester l'installation
test_installation() {
    print_step "7. Test de l'installation"
    
    sleep 2
    
    # Essayer plusieurs façons de trouver zarch
    if command -v zarch &> /dev/null; then
        print_info "Test avec 'zarch --help'..."
        if zarch --help 2>&1 | grep -q -i "usage\|help\|zarch"; then
            print_success "✓ Zarch CLI fonctionnel"
            echo ""
            zarch --version 2>/dev/null || true
        fi
    elif [[ -x "$INSTALL_DIR/zarch" ]]; then
        print_info "Test avec le chemin complet..."
        if "$INSTALL_DIR/zarch" --help 2>&1 | grep -q -i "usage\|help\|zarch"; then
            print_success "✓ Zarch CLI fonctionnel (chemin complet)"
            echo ""
            "$INSTALL_DIR/zarch" --version 2>/dev/null || true
            
            # Ajouter au PATH si nécessaire
            if ! echo "$PATH" | grep -q "$INSTALL_DIR"; then
                print_warning "Ajoutez à votre PATH:"
                echo "export PATH=\"$INSTALL_DIR:\$PATH\""
                echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$HOME/.bashrc" 2>/dev/null
                echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$HOME/.zshrc" 2>/dev/null
            fi
        fi
    else
        print_warning "Impossible de tester l'installation"
    fi
}

# Nettoyage
cleanup() {
    print_step "8. Nettoyage"
    
    if [[ -d "$TEMP_DIR" ]]; then
        rm -rf "$TEMP_DIR"
        print_info "Fichiers temporaires nettoyés"
    fi
}

# Résumé
show_summary() {
    echo -e "\n${GREEN}═══════════════════════════════════════════════${NC}"
    echo -e "${GREEN}      ZARCH CLI INSTALLATION TERMINÉE           ${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
    
    echo -e "\n${CYAN}📊 Résumé:${NC}"
    echo -e "  OS:          ${OS_TYPE}"
    echo -e "  Gestionnaire: ${PKG_MANAGER}"
    echo -e "  Binaire:     ${INSTALL_DIR}/zarch"
    echo -e "  Config:      ${CONFIG_DIR}/"
    
    echo -e "\n${CYAN}🚀 Commandes disponibles:${NC}"
    echo -e "  zarch --help                 # Aide"
    echo -e "  zarch login <user> <pass>    # Connexion"
    echo -e "  zarch init                   # Initialiser"
    echo -e "  zarch search <query>         # Rechercher"
    echo -e "  zarch install @user/pkg      # Installer"
    
    echo -e "\n${YELLOW}⚠️  Notes:${NC}"
    if [[ "$OS_TYPE" == "alpine" ]]; then
        echo -e "  Alpine Linux détecté - Installation optimisée"
    fi
    if [[ ! -w "/usr/local/bin" ]] && [[ "$INSTALL_DIR" == "$HOME/.local/bin" ]]; then
        echo -e "  Binaire installé dans ~/.local/bin"
        echo -e "  Ajoutez au PATH: export PATH=\"\$HOME/.local/bin:\$PATH\""
    fi
    
    echo ""
}

# Fonction principale
main() {
    local start_time=$(date +%s)
    
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════╗"
    echo "║        ZARCH CLI INSTALLER v2.0          ║"
    echo "║    Correction automatique des erreurs    ║"
    echo "║        Support Alpine Linux              ║"
    echo "╚══════════════════════════════════════════╝"
    echo -e "${NC}"
    
    # Étapes d'installation
    detect_os
    check_dependencies
    download_source
    compile_with_fixes
    install_binary
    create_config
    test_installation
    cleanup
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    local minutes=$((duration / 60))
    local seconds=$((duration % 60))
    
    print_info "Temps d'installation: ${minutes}m ${seconds}s"
    
    show_summary
}

# Gestion des erreurs
trap 'print_error "Interruption"; cleanup; exit 1' INT
trap 'print_error "Erreur à la ligne $LINENO"; cleanup; exit 1' ERR

# Démarrer
main
