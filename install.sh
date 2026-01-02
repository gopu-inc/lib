#!/bin/bash

# Zarch CLI Installer v2.0 - Version non-interactive
# Installation sécurisée depuis GitHub

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

# Vérifier si on est root - version simplifiée
check_root() {
    if [[ $EUID -eq 0 ]]; then
        print_warning "L'installation est exécutée en tant que root."
        print_warning "Le CLI sera installé dans $INSTALL_DIR"
        print_warning "Les fichiers de configuration seront dans /root/.zarch"
        print_info "Installation en cours..."
    fi
}

# Vérifier les dépendances - version simplifiée
check_dependencies() {
    print_step "1. Vérification des dépendances"
    
    # Vérifier gcc
    if ! command -v gcc &> /dev/null; then
        print_error "GCC n'est pas installé"
        print_info "Installation automatique de gcc..."
        
        if [[ -f /etc/debian_version ]]; then
            apt-get update && apt-get install -y gcc
        elif [[ -f /etc/redhat-release ]]; then
            yum install -y gcc
        elif [[ "$OSTYPE" == "darwin"* ]]; then
            xcode-select --install || brew install gcc
        fi
    fi
    
    # Vérifier curl
    if ! command -v curl &> /dev/null; then
        print_info "Installation de curl..."
        if [[ -f /etc/debian_version ]]; then
            apt-get install -y curl
        elif [[ -f /etc/redhat-release ]]; then
            yum install -y curl
        elif [[ "$OSTYPE" == "darwin"* ]]; then
            brew install curl
        fi
    fi
    
    print_success "Dépendances vérifiées"
}

# ... [le reste des fonctions reste similaire] ...

# Fonction principale
main() {
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════╗"
    echo "║         INSTALLATION ZARCH CLI           ║"
    echo "║         Version 2.0 - Non-interactive    ║"
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
trap 'print_error "Installation interrompue"; exit 1' INT
trap 'print_error "Erreur à la ligne $LINENO"; exit 1' ERR

# Exécuter l'installation
main
