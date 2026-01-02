#!/bin/bash

# Zarch CLI Installer v2.0 - Installation automatique
# Détection OS et installation silencieuse des dépendances

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

# Détection OS et package manager
OS_TYPE=""
PKG_MANAGER=""
PKG_INSTALL=""
PKG_UPDATE=""

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

# Détecter l'OS et le gestionnaire de paquets
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
        PKG_INSTALL="apt-get install -y"
        PKG_UPDATE="apt-get update"
        print_info "Gestionnaire: APT (Debian/Ubuntu)"
    elif command -v yum &> /dev/null; then
        PKG_MANAGER="yum"
        PKG_INSTALL="yum install -y"
        PKG_UPDATE="yum update -y"
        print_info "Gestionnaire: YUM (RHEL/CentOS)"
    elif command -v dnf &> /dev/null; then
        PKG_MANAGER="dnf"
        PKG_INSTALL="dnf install -y"
        PKG_UPDATE="dnf update -y"
        print_info "Gestionnaire: DNF (Fedora)"
    elif command -v pacman &> /dev/null; then
        PKG_MANAGER="pacman"
        PKG_INSTALL="pacman -S --noconfirm"
        PKG_UPDATE="pacman -Sy"
        print_info "Gestionnaire: Pacman (Arch)"
    elif command -v apk &> /dev/null; then
        PKG_MANAGER="apk"
        PKG_INSTALL="apk add"
        PKG_UPDATE="apk update"
        print_info "Gestionnaire: APK (Alpine)"
    elif command -v brew &> /dev/null; then
        PKG_MANAGER="brew"
        PKG_INSTALL="brew install"
        PKG_UPDATE="brew update"
        print_info "Gestionnaire: Homebrew (macOS)"
    elif command -v pkg &> /dev/null; then
        PKG_MANAGER="pkg"
        PKG_INSTALL="pkg install -y"
        PKG_UPDATE="pkg update"
        print_info "Gestionnaire: PKG (FreeBSD)"
    elif command -v zypper &> /dev/null; then
        PKG_MANAGER="zypper"
        PKG_INSTALL="zypper install -y"
        PKG_UPDATE="zypper refresh"
        print_info "Gestionnaire: Zypper (openSUSE)"
    else
        print_warning "Aucun gestionnaire de paquets détecté"
    fi
}

# Vérifier si on est root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        print_warning "Installation en tant que root"
        print_info "CLI: $INSTALL_DIR/zarch"
        print_info "Config: /root/.zarch"
    else
        # Si non-root, utiliser ~/.local/bin
        LOCAL_BIN="$HOME/.local/bin"
        if [[ ! -w "/usr/local/bin" ]] && [[ ! -w "/usr/bin" ]]; then
            INSTALL_DIR="$LOCAL_BIN"
            mkdir -p "$INSTALL_DIR"
            print_info "Installation non-root dans: $INSTALL_DIR"
            print_info "Ajoutez à votre PATH: export PATH=\"\$HOME/.local/bin:\$PATH\""
        fi
    fi
}

# Installer une dépendance si manquante
install_if_missing() {
    local cmd=$1
    local pkg=${2:-$1}
    
    if ! command -v "$cmd" &> /dev/null; then
        print_info "Installation de $pkg..."
        
        if [[ -n "$PKG_MANAGER" ]]; then
            if [[ "$PKG_MANAGER" == "brew" ]]; then
                # Homebrew sur macOS
                if [[ "$pkg" == "gcc" ]]; then
                    brew install gcc || true
                else
                    $PKG_INSTALL "$pkg" || true
                fi
            elif [[ "$PKG_MANAGER" == "apk" ]]; then
                # Alpine Linux
                case "$pkg" in
                    gcc) $PKG_INSTALL build-base || true ;;
                    curl) $PKG_INSTALL curl || true ;;
                    jansson) $PKG_INSTALL jansson-dev || true ;;
                    libssl) $PKG_INSTALL openssl-dev || true ;;
                    *) $PKG_INSTALL "$pkg" || true ;;
                esac
            elif [[ "$PKG_MANAGER" == "apt" ]]; then
                # Debian/Ubuntu
                case "$pkg" in
                    gcc) $PKG_INSTALL build-essential || true ;;
                    jansson) $PKG_INSTALL libjansson-dev || true ;;
                    libssl) $PKG_INSTALL libssl-dev || true ;;
                    libcurl) $PKG_INSTALL libcurl4-openssl-dev || true ;;
                    *) $PKG_INSTALL "$pkg" || true ;;
                esac
            elif [[ "$PKG_MANAGER" == "yum" || "$PKG_MANAGER" == "dnf" ]]; then
                # RHEL/CentOS/Fedora
                case "$pkg" in
                    gcc) $PKG_INSTALL gcc make || true ;;
                    jansson) $PKG_INSTALL jansson-devel || true ;;
                    libssl) $PKG_INSTALL openssl-devel || true ;;
                    libcurl) $PKG_INSTALL libcurl-devel || true ;;
                    *) $PKG_INSTALL "$pkg" || true ;;
                esac
            elif [[ "$PKG_MANAGER" == "pacman" ]]; then
                # Arch Linux
                $PKG_INSTALL "$pkg" || true
            else
                print_warning "Impossible d'installer $pkg automatiquement"
                return 1
            fi
            
            # Vérifier si l'installation a réussi
            if command -v "$cmd" &> /dev/null; then
                print_success "$pkg installé"
            else
                print_warning "Échec de l'installation de $pkg"
            fi
        fi
    else
        print_info "✓ $cmd déjà installé"
    fi
}

# Vérifier les dépendances
check_dependencies() {
    print_step "1. Vérification des dépendances"
    
    # Mettre à jour les paquets
    if [[ -n "$PKG_UPDATE" ]] && [[ "$PKG_MANAGER" != "brew" ]]; then
        print_info "Mise à jour des paquets..."
        eval "$PKG_UPDATE" 2>/dev/null || true
    fi
    
    # Liste des dépendances essentielles
    local essential_deps=("gcc" "curl" "make")
    
    for dep in "${essential_deps[@]}"; do
        install_if_missing "$dep"
    done
    
    # Dépendances pour la compilation
    print_info "Installation des bibliothèques de développement..."
    
    case "$PKG_MANAGER" in
        apt)
            install_if_missing "libjansson" "libjansson-dev"
            install_if_missing "libssl" "libssl-dev"
            install_if_missing "libcurl" "libcurl4-openssl-dev"
            install_if_missing "zlib" "zlib1g-dev"
            ;;
        yum|dnf)
            install_if_missing "jansson" "jansson-devel"
            install_if_missing "openssl" "openssl-devel"
            install_if_missing "curl" "libcurl-devel"
            install_if_missing "zlib" "zlib-devel"
            ;;
        apk)
            install_if_missing "jansson" "jansson-dev"
            install_if_missing "openssl" "openssl-dev"
            install_if_missing "curl-dev" "curl-dev"
            install_if_missing "zlib" "zlib-dev"
            ;;
        pacman)
            install_if_missing "jansson"
            install_if_missing "openssl"
            install_if_missing "curl"
            install_if_missing "zlib"
            ;;
        brew)
            install_if_missing "jansson"
            install_if_missing "openssl"
            install_if_missing "curl"
            install_if_missing "zlib"
            ;;
    esac
    
    print_success "Dépendances vérifiées"
}

# Créer les répertoires
create_directories() {
    print_step "2. Préparation des répertoires"
    
    # Créer le répertoire temporaire
    mkdir -p "$TEMP_DIR"
    print_info "Répertoire temporaire: $TEMP_DIR"
    
    # Créer le répertoire de configuration
    mkdir -p "$CONFIG_DIR" "$CACHE_DIR"
    chmod 700 "$CONFIG_DIR" 2>/dev/null || true
    print_info "Répertoire de configuration: $CONFIG_DIR"
}

# Télécharger le code source
download_source() {
    print_step "3. Téléchargement du code source"
    
    print_info "Téléchargement depuis GitHub..."
    
    # Essayer plusieurs URLs
    local urls=(
        "$ZARCH_SOURCE_URL"
        "https://raw.githubusercontent.com/gopu-inc/lib/main/zarch.c"
        "https://cdn.jsdelivr.net/gh/gopu-inc/lib@main/zarch.c"
    )
    
    for url in "${urls[@]}"; do
        print_info "Essai: $url"
        if curl -sSL -f -o "$TEMP_DIR/zarch.c" "$url" 2>/dev/null; then
            print_success "Code source téléchargé"
            
            # Vérifier le fichier
            if [[ -s "$TEMP_DIR/zarch.c" ]] && grep -q "#include" "$TEMP_DIR/zarch.c"; then
                print_info "Fichier valide: $(wc -l < "$TEMP_DIR/zarch.c") lignes"
                return 0
            fi
        fi
        sleep 1
    done
    
    print_error "Impossible de télécharger le code source"
    exit 1
}

# Compiler le CLI
compile_cli() {
    print_step "4. Compilation (peut prendre 2-3 minutes)"
    
    local cflags="-Wall -O2 -D_DEFAULT_SOURCE -std=c99"
    local libs="-lcurl -ljansson -lcrypto -lz"
    
    # Flags spécifiques à l'OS
    case "$OS_TYPE" in
        macos|darwin*)
            cflags="$cflags -I/usr/local/include -L/usr/local/lib"
            if [[ -d "/opt/homebrew" ]]; then
                cflags="$cflags -I/opt/homebrew/include -L/opt/homebrew/lib"
            fi
            ;;
        alpine)
            libs="$libs -lm"
            ;;
    esac
    
    local compile_cmd="gcc $cflags -o \"$TEMP_DIR/zarch\" \"$TEMP_DIR/zarch.c\" $libs"
    
    print_info "Compilation en cours..."
    print_info "Commande: $compile_cmd"
    
    cd "$TEMP_DIR"
    
    # Compilation avec timeout
    if timeout 180 bash -c "$compile_cmd" 2>&1; then
        if [[ -f "zarch" ]] && [[ -x "zarch" ]]; then
            print_success "Compilation réussie!"
            print_info "Taille: $(du -h zarch | cut -f1)"
        else
            print_error "Binaire non créé"
            exit 1
        fi
    else
        print_error "Échec de la compilation"
        
        # Afficher des informations de débogage
        print_info "Test de compilation simple..."
        echo "#include <stdio.h>
int main() { printf(\"Test\\n\"); return 0; }" > test.c
        gcc test.c -o test && ./test && rm -f test test.c
        
        exit 1
    fi
}

# Installer le binaire
install_binary() {
    print_step "5. Installation du binaire"
    
    # Vérifier les permissions
    local target_dir="$INSTALL_DIR"
    
    if [[ ! -w "$target_dir" ]] && [[ $EUID -ne 0 ]]; then
        print_info "Utilisation de sudo pour l'installation..."
        if sudo mv "$TEMP_DIR/zarch" "$target_dir/zarch" 2>/dev/null; then
            sudo chmod +x "$target_dir/zarch" 2>/dev/null || true
        else
            # Fallback vers ~/.local/bin
            target_dir="$HOME/.local/bin"
            mkdir -p "$target_dir"
            mv "$TEMP_DIR/zarch" "$target_dir/zarch"
            chmod +x "$target_dir/zarch"
        fi
    else
        mv "$TEMP_DIR/zarch" "$target_dir/zarch"
        chmod +x "$target_dir/zarch"
    fi
    
    INSTALL_DIR="$target_dir"
    print_success "Installé dans: $INSTALL_DIR/zarch"
    
    # Vérifier le PATH
    if echo "$PATH" | grep -q "$INSTALL_DIR"; then
        print_info "✓ Répertoire déjà dans PATH"
    else
        print_warning "Ajoutez à votre shell:"
        echo "export PATH=\"$INSTALL_DIR:\$PATH\""
        echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$HOME/.bashrc" 2>/dev/null || true
        echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$HOME/.zshrc" 2>/dev/null || true
    fi
}

# Créer la configuration
create_config() {
    print_step "6. Configuration initiale"
    
    # Fichier de configuration
    local config_file="$CONFIG_DIR/config.json"
    
    cat > "$config_file" << EOF
{
    "registry_url": "https://zenv-hub.onrender.com",
    "api_version": "v1",
    "auto_update": true,
    "cache_enabled": true,
    "cache_ttl": 3600,
    "timeout": 30,
    "max_retries": 3,
    "version": "2.0.0",
    "os": "$OS_TYPE",
    "install_dir": "$INSTALL_DIR",
    "last_updated": "$(date -Iseconds)"
}
EOF
    
    chmod 600 "$config_file" 2>/dev/null || true
    
    # Fichier de cache
    echo "{}" > "$CACHE_DIR/packages.json"
    
    print_success "Configuration créée"
}

# Tester l'installation
test_installation() {
    print_step "7. Test de l'installation"
    
    sleep 2
    
    if command -v zarch >/dev/null 2>&1; then
        print_info "Test de la commande zarch..."
        
        # Tester différentes commandes
        if zarch --help 2>&1 | grep -q -i "usage\|help\|zarch"; then
            print_success "✓ CLI fonctionnel"
            echo ""
            zarch --version 2>/dev/null || zarch --help 2>&1 | head -5
        else
            print_warning "Sortie inattendue, mais binaire présent"
        fi
    else
        # Essayer avec le chemin complet
        if [[ -x "$INSTALL_DIR/zarch" ]]; then
            print_info "Test avec chemin complet..."
            "$INSTALL_DIR/zarch" --version 2>/dev/null || "$INSTALL_DIR/zarch" --help 2>&1 | head -3
        fi
        
        print_warning "Rechargez votre shell:"
        echo "source ~/.bashrc || source ~/.zshrc"
        echo "ou"
        echo "export PATH=\"$INSTALL_DIR:\$PATH\""
    fi
}

# Nettoyer
cleanup() {
    print_step "8. Nettoyage"
    
    if [[ -d "$TEMP_DIR" ]]; then
        rm -rf "$TEMP_DIR"
        print_info "Fichiers temporaires nettoyés"
    fi
}

# Afficher le résumé
show_summary() {
    echo -e "\n${GREEN}═══════════════════════════════════════════════${NC}"
    echo -e "${GREEN}      ZARCH CLI INSTALLÉ AVEC SUCCÈS            ${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
    
    echo -e "\n${CYAN}📊 Détails de l'installation:${NC}"
    echo -e "  OS:          ${OS_TYPE:-Inconnu}"
    echo -e "  Gestionnaire: ${PKG_MANAGER:-Aucun}"
    echo -e "  Binaire:     ${INSTALL_DIR}/zarch"
    echo -e "  Config:      ${CONFIG_DIR}/"
    echo -e "  Durée:       ~3-5 minutes"
    
    echo -e "\n${CYAN}🚀 Pour commencer:${NC}"
    echo -e "  zarch --help                    # Afficher l'aide"
    echo -e "  zarch login                     # Se connecter"
    echo -e "  zarch init                      # Initialiser un projet"
    echo -e "  zarch search <query>            # Rechercher packages"
    
    echo -e "\n${CYAN}🔧 Dépannage:${NC}"
    echo -e "  Si 'zarch' n'est pas trouvé:"
    echo -e "    source ~/.bashrc"
    echo -e "    # ou"
    echo -e "    export PATH=\"$INSTALL_DIR:\$PATH\""
    
    echo -e "\n${YELLOW}⚠️  Support:${NC}"
    echo -e "  Documentation: https://docs.zenv-hub.onrender.com"
    echo -e "  GitHub:        https://github.com/gopu-inc/lib"
    echo -e "  Registry:      https://zenv-hub.onrender.com"
    
    echo ""
}

# Fonction principale avec timing
main() {
    local start_time=$(date +%s)
    
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════╗"
    echo "║        ZARCH CLI INSTALLER v2.0          ║"
    echo "║        Installation automatique          ║"
    echo "║        Durée estimée: 3-5 minutes       ║"
    echo "╚══════════════════════════════════════════╝"
    echo -e "${NC}"
    
    # Détection OS en premier
    detect_os
    
    # Exécution séquentielle avec timing
    check_root
    check_dependencies
    create_directories
    download_source
    compile_cli
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

# Démarrer l'installation
main
