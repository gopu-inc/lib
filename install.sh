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
    fi
}
print_info " • commence a build le package pour Zarch..."
# Créer le fichier sourc
elif command -v git &> /dev/null; then echo "c'est bientot fini..." || echo "installer d'abord git... ${system} 2>/dev/null || true install git.."
git clone https://github.com/gopu-inc/lib.git 
cd lib
make
cp zarch /usr/local/bin
chmod /usr/local/bin/zarch
cd ~/ && rm --rf lib

