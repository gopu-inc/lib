#!/bin/bash

# Zarch CLI Installer v2.0
# Installation sécurisée depuis GitHub
# URL: https://raw.githubusercontent.com/gopu-inc/lib/refs/heads/main/install.sh

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
}

# Vérifier si on est root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        print_warning "L'installation est exécutée en tant que root."
        print_warning "Le CLI sera installé dans $INSTALL_DIR"
        print_warning "Les fichiers de configuration seront dans /root/.zarch"
        read -p "Continuer ? (o/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Oo]$ ]]; then
            exit 1
        fi
    fi
}

# Vérifier les dépendances
check_dependencies() {
    print_step "1. Vérification des dépendances"
    
    local missing_deps=()
    
    # Vérifier gcc
    if ! command -v gcc &> /dev/null; then
        print_error "GCC n'est pas installé"
        missing_deps+=("gcc")
    else
        print_info "✓ GCC trouvé: $(gcc --version | head -n1)"
    fi
    
    # Vérifier curl
    if ! command -v curl &> /dev/null; then
        print_error "curl n'est pas installé"
        missing_deps+=("curl")
    else
        print_info "✓ curl trouvé"
    fi
    
    # Vérifier make
    if ! command -v make &> /dev/null; then
        print_warning "make n'est pas installé (optionnel)"
    fi
    
    # Vérifier les bibliothèques de développement
    check_libraries() {
        local libs=("libcurl" "libjansson" "libcrypto" "libz")
        for lib in "${libs[@]}"; do
            if ldconfig -p | grep -q "$lib"; then
                print_info "✓ $lib trouvé"
            else
                print_warning "$lib n'est pas installé"
            fi
        done
    }
    
    check_libraries
    
    # Installer les dépendances manquantes si demandé
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        print_warning "Dépendances manquantes: ${missing_deps[*]}"
        
        if [[ -f /etc/debian_version ]]; then
            # Debian/Ubuntu
            print_info "Détection: Système Debian/Ubuntu"
            read -p "Installer les dépendances automatiquement ? (o/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Oo]$ ]]; then
                sudo apt-get update
                sudo apt-get install -y "${missing_deps[@]}" libcurl4-openssl-dev libjansson-dev libssl-dev zlib1g-dev
            else
                print_error "Veuillez installer les dépendances manuellement:"
                echo "sudo apt-get install ${missing_deps[*]} libcurl4-openssl-dev libjansson-dev libssl-dev zlib1g-dev"
                exit 1
            fi
        elif [[ "$OSTYPE" == "darwin"* ]]; then
            # macOS
            print_info "Détection: macOS"
            if ! command -v brew &> /dev/null; then
                print_error "Homebrew n'est pas installé. Installez-le depuis https://brew.sh"
                exit 1
            fi
            read -p "Installer les dépendances avec Homebrew ? (o/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Oo]$ ]]; then
                brew install "${missing_deps[@]}" curl jansson openssl zlib
                # Configurer les chemins pour macOS
                export PKG_CONFIG_PATH="/usr/local/opt/openssl/lib/pkgconfig:/usr/local/opt/curl/lib/pkgconfig:$PKG_CONFIG_PATH"
                export LDFLAGS="-L/usr/local/opt/openssl/lib -L/usr/local/opt/curl/lib"
                export CPPFLAGS="-I/usr/local/opt/openssl/include -I/usr/local/opt/curl/include"
            else
                print_error "Veuillez installer les dépendances manuellement:"
                echo "brew install ${missing_deps[*]} curl jansson openssl zlib"
                exit 1
            fi
        else
            print_error "Système non supporté. Installez manuellement: ${missing_deps[*]}"
            exit 1
        fi
    fi
}

# Créer les répertoires
create_directories() {
    print_step "2. Préparation des répertoires"
    
    # Créer le répertoire temporaire
    mkdir -p "$TEMP_DIR"
    print_info "Répertoire temporaire: $TEMP_DIR"
    
    # Créer le répertoire de configuration
    mkdir -p "$CONFIG_DIR" "$CACHE_DIR"
    chmod 700 "$CONFIG_DIR"
    print_info "Répertoire de configuration: $CONFIG_DIR"
}

# Télécharger le code source
download_source() {
    print_step "3. Téléchargement du code source"
    
    print_info "Téléchargement depuis: $ZARCH_SOURCE_URL"
    
    # Télécharger le fichier source
    if ! curl -sSL -o "$TEMP_DIR/zarch.c" "$ZARCH_SOURCE_URL"; then
        print_error "Échec du téléchargement du code source"
        
        # Essayer une URL alternative
        ZARCH_SOURCE_URL_ALT="https://raw.githubusercontent.com/gopu-inc/lib/main/zarch.c"
        print_info "Essai d'une URL alternative: $ZARCH_SOURCE_URL_ALT"
        
        if ! curl -sSL -o "$TEMP_DIR/zarch.c" "$ZARCH_SOURCE_URL_ALT"; then
            print_error "Échec du téléchargement depuis toutes les sources"
            exit 1
        fi
    fi
    
    # Vérifier que le fichier est valide (doit contenir du code C)
    if ! grep -q "#include" "$TEMP_DIR/zarch.c"; then
        print_error "Le fichier téléchargé ne semble pas être du code C valide"
        print_info "Premières lignes du fichier:"
        head -5 "$TEMP_DIR/zarch.c"
        exit 1
    fi
    
    print_success "Code source téléchargé avec succès"
    print_info "Taille du fichier: $(wc -l < "$TEMP_DIR/zarch.c") lignes"
}

# Compiler le CLI
compile_cli() {
    print_step "4. Compilation du CLI"
    
    local compile_cmd="gcc -o \"$TEMP_DIR/zarch\" \"$TEMP_DIR/zarch.c\" \
        -lcurl -ljansson -lcrypto -lz \
        -Wall -O2 -D_DEFAULT_SOURCE \
        -std=c99"
    
    # Ajouter des flags supplémentaires pour macOS si nécessaire
    if [[ "$OSTYPE" == "darwin"* ]]; then
        compile_cmd="$compile_cmd -I/usr/local/include -L/usr/local/lib"
    fi
    
    print_info "Commande de compilation:"
    echo "$compile_cmd" | sed 's/        / /g'
    
    # Exécuter la compilation
    cd "$TEMP_DIR"
    if eval "$compile_cmd"; then
        print_success "Compilation réussie !"
        
        # Vérifier que le binaire est exécutable
        if [[ -x "zarch" ]]; then
            print_info "✓ Binaire créé: $(file zarch)"
            print_info "✓ Taille: $(du -h zarch | cut -f1)"
        else
            print_error "Le binaire n'est pas exécutable"
            exit 1
        fi
    else
        print_error "Échec de la compilation"
        print_info "Vérification des erreurs..."
        
        # Essayer une compilation simple pour diagnostiquer
        print_info "Tentative de compilation diagnostique..."
        gcc -c zarch.c -o zarch_test.o 2>&1 | head -20 || true
        
        exit 1
    fi
}

# Installer le binaire
install_binary() {
    print_step "5. Installation du binaire"
    
    # Vérifier les permissions
    if [[ ! -w "$INSTALL_DIR" ]] && [[ $EUID -ne 0 ]]; then
        print_warning "Permissions insuffisantes pour écrire dans $INSTALL_DIR"
        print_info "Utilisation de sudo pour l'installation..."
        
        if sudo mv "$TEMP_DIR/zarch" "$INSTALL_DIR/zarch"; then
            sudo chmod +x "$INSTALL_DIR/zarch"
            print_success "Installé dans $INSTALL_DIR/zarch (avec sudo)"
        else
            print_error "Échec de l'installation avec sudo"
            print_info "Installation alternative dans ~/.local/bin..."
            
            LOCAL_BIN="$HOME/.local/bin"
            mkdir -p "$LOCAL_BIN"
            mv "$TEMP_DIR/zarch" "$LOCAL_BIN/zarch"
            chmod +x "$LOCAL_BIN/zarch"
            INSTALL_DIR="$LOCAL_BIN"
            print_success "Installé dans $INSTALL_DIR/zarch"
        fi
    else
        mv "$TEMP_DIR/zarch" "$INSTALL_DIR/zarch"
        chmod +x "$INSTALL_DIR/zarch"
        print_success "Installé dans $INSTALL_DIR/zarch"
    fi
    
    # Vérifier que le binaire est dans le PATH
    if command -v zarch &> /dev/null; then
        print_success "✓ Le CLI 'zarch' est maintenant disponible dans votre PATH"
    else
        print_warning "Le binaire n'est pas dans votre PATH"
        print_info "Ajoutez ceci à votre ~/.bashrc ou ~/.zshrc:"
        echo "export PATH=\"$INSTALL_DIR:\$PATH\""
    fi
}

# Créer la configuration initiale
create_config() {
    print_step "6. Configuration initiale"
    
    # Fichier de configuration par défaut
    local config_file="$CONFIG_DIR/config.json"
    
    if [[ ! -f "$config_file" ]]; then
        cat > "$config_file" << EOF
{
    "registry_url": "https://zenv-hub.onrender.com",
    "auto_update": true,
    "cache_enabled": true,
    "timeout": 30,
    "version": "2.0.0"
}
EOF
        print_info "✓ Fichier de configuration créé: $config_file"
    fi
    
    # Fichier de cache des packages
    local cache_file="$CACHE_DIR/packages.json"
    if [[ ! -f "$cache_file" ]]; then
        echo "{}" > "$cache_file"
        print_info "✓ Cache initialisé: $cache_file"
    fi
    
    # Set permissions
    chmod 600 "$config_file"
    chmod 644 "$cache_file"
}

# Tester l'installation
test_installation() {
    print_step "7. Test de l'installation"
    
    if command -v zarch &> /dev/null; then
        print_info "Test de la commande 'zarch --help'..."
        
        if zarch --help 2>&1 | grep -q "Zarch\|Usage"; then
            print_success "✓ Installation testée avec succès"
            
            # Afficher la version
            echo
            zarch --version 2>/dev/null || true
        else
            print_warning "L'installation semble complète mais le test a échoué"
            print_info "Sortie de 'zarch --help':"
            zarch --help 2>&1 | head -10
        fi
    else
        print_warning "La commande 'zarch' n'est pas trouvée dans le PATH"
        print_info "Essayez de recharger votre shell:"
        echo "source ~/.bashrc  # ou source ~/.zshrc"
        echo "zarch --help"
    fi
}

# Nettoyer les fichiers temporaires
cleanup() {
    print_step "8. Nettoyage"
    
    if [[ -d "$TEMP_DIR" ]]; then
        rm -rf "$TEMP_DIR"
        print_info "✓ Fichiers temporaires nettoyés"
    fi
}

# Afficher le résumé
show_summary() {
    print_step "✅ Installation terminée !"
    
    echo -e "\n${GREEN}╔══════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║        ZARCH CLI INSTALLÉ AVEC SUCCÈS      ║${NC}"
    echo -e "${GREEN}╚══════════════════════════════════════════╝${NC}"
    
    echo -e "\n${CYAN}📁 Emplacements:${NC}"
    echo -e "  Binaire:      ${INSTALL_DIR}/zarch"
    echo -e "  Configuration: ${CONFIG_DIR}/"
    echo -e "  Cache:        ${CACHE_DIR}/"
    
    echo -e "\n${CYAN}🚀 Commandes disponibles:${NC}"
    echo -e "  zarch --help                 # Afficher l'aide"
    echo -e "  zarch login <user> <pass>    # Se connecter"
    echo -e "  zarch init .                 # Initialiser un package"
    echo -e "  zarch publish . user         # Publier un package"
    echo -e "  zarch install @user/pkg      # Installer un package"
    echo -e "  zarch search \"query\"         # Rechercher"
    
    echo -e "\n${CYAN}🔗 Ressources:${NC}"
    echo -e "  Registry:     https://zenv-hub.onrender.com"
    echo -e "  Documentation: https://docs.zenv-hub.onrender.com"
    echo -e "  GitHub:       https://github.com/gopu-inc/lib"
    
    echo -e "\n${YELLOW}⚠️  Note:${NC} Si 'zarch' n'est pas reconnu, rechargez votre shell:"
    echo -e "  source ~/.bashrc  # ou source ~/.zshrc\n"
}

# Fonction principale
main() {
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════╗"
    echo "║         INSTALLATION ZARCH CLI           ║"
    echo "║         Version 2.0 - GitHub Source      ║"
    echo "╚══════════════════════════════════════════╝"
    echo -e "${NC}"
    
    # Exécuter toutes les étapes
    check_root
    check_dependencies
    create_directories
    download_source
    compile_cli
    install_binary
    create_config
    test_installation
    cleanup
    show_summary
    
    exit 0
}

# Gestion des erreurs
trap 'print_error "Installation interrompue par l utilisateur"; exit 1' INT
trap 'print_error "Erreur à la ligne $LINENO"; exit 1' ERR

# Exécuter l'installation
main
