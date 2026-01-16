#!/bin/bash
# Installateur Zarch CLI avec libarchive

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

print_error() { echo -e "${RED}[ERREUR]${NC} $1" >&2; }
print_success() { echo -e "${GREEN}[SUCCÈS]${NC} $1"; }
print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[AVERTISSEMENT]${NC} $1"; }

# Vérifier les dépendances
check_dependencies() {
    print_info "Vérification des dépendances..."
    
    # Pour Alpine Linux
    if command -v apk &> /dev/null; then
        print_info "Installation des dépendances Alpine..."
        apk add --no-cache gcc make curl jansson-dev openssl-dev curl-dev zlib-dev libarchive-dev musl-dev
    # Pour Debian/Ubuntu
    elif command -v apt &> /dev/null; then
        print_info "Installation des dépendances Debian..."
        apt-get update
        apt-get install -y gcc make curl libjansson-dev libssl-dev libcurl4-openssl-dev zlib1g-dev libarchive-dev
    # Pour RHEL/CentOS
    elif command -v yum &> /dev/null; then
        print_info "Installation des dépendances RHEL..."
        yum install -y gcc make curl jansson-devel openssl-devel libcurl-devel zlib-devel libarchive-devel
    else
        print_warning "OS non supporté, installation manuelle requise"
        return 1
    fi
}

# Installation de Zarch
install_zarch() {
    print_info "Construction du package pour Zarch..."
    
    if ! command -v git &> /dev/null; then
        print_error "Git n'est pas installé"
        print_info "Installation de git..."
        
        if command -v apk &> /dev/null; then
            apk add --no-cache git
        elif command -v apt &> /dev/null; then
            apt-get install -y git
        elif command -v yum &> /dev/null; then
            yum install -y git
        else
            print_error "Impossible d'installer git automatiquement"
            exit 1
        fi
    fi
    
    # Cloner le dépôt
    if [ -d "lib" ]; then
        print_warning "Le dossier 'lib' existe déjà, suppression..."
        rm -rf lib
    fi
    
    print_info "Clonage du dépôt..."
    git clone https://github.com/gopu-inc/lib.git || {
        print_error "Échec du clonage du dépôt"
        exit 1
    }
    
    cd lib || {
        print_error "Impossible d'entrer dans le dossier 'lib'"
        exit 1
    }
    
    # Compilation
    print_info "Compilation..."
    make || {
        print_error "Échec de la compilation"
        exit 1
    }
    
    # Installation
    print_info "Installation de Zarch..."
    cp zarch /usr/local/bin/ || {
        print_error "Échec de la copie de zarch vers /usr/local/bin/"
        exit 1
    }
    
    chmod +x /usr/local/bin/zarch || {
        print_error "Échec du changement des permissions"
        exit 1
    }
    
    cd ~/ || exit
    rm -rf lib
    
    print_success "Zarch a été installé avec succès !"
    print_info "Vous pouvez maintenant utiliser la commande 'zarch'"
}

# Fonction principale
main() {
    print_info "Début de l'installation de Zarch CLI..."
    
    # Vérifier si l'utilisateur est root
    if [ "$EUID" -ne 0 ]; then 
        print_warning "Il est recommandé d'exécuter ce script en tant que root"
        read -p "Continuer quand même ? (o/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[OoYy]$ ]]; then
            print_error "Installation annulée"
            exit 1
        fi
    fi
    
    # Installer les dépendances
    check_dependencies
    
    # Installer Zarch
    install_zarch
}

# Exécution du script
main "$@"
