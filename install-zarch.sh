#!/bin/sh
# Installateur officiel de zarch pour iSH
# Dépôt: https://github.com/gopu-inc/lib

set -e  # Arrêter en cas d'erreur

echo "========================================"
echo "🐧 INSTALLATEUR ZARCH pour iSH"
echo "========================================"

# Vérifier si on est sur iSH
if [ ! -f /etc/alpine-release ]; then
    echo "⚠️  Attention: Ce script est conçu pour iSH (Alpine Linux)"
    echo "   Vous semblez être sur un autre système."
    read -p "Continuer quand même? (o/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Oo]$ ]]; then
        exit 1
    fi
fi

# Vérifier les permissions
if [ "$(id -u)" -ne 0 ]; then
    echo "🔑 Nécessite les droits root (sudo)"
    echo "   Le script va utiliser sudo automatiquement"
fi

# URL du dépôt
REPO="https://raw.githubusercontent.com/gopu-inc/lib/main"
ZARCH_URL="$REPO/zarch"
PKG_URL="$REPO/packages.json"

# Fonction pour exécuter avec sudo si nécessaire
sudo_cmd() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

# Fonction pour télécharger
download_file() {
    local url="$1"
    local dest="$2"
    
    echo "📥 Téléchargement: $(basename "$dest")"
    
    if command -v wget >/dev/null 2>&1; then
        sudo_cmd wget -q -O "$dest" "$url"
    elif command -v curl >/dev/null 2>&1; then
        sudo_cmd curl -s -L -o "$dest" "$url"
    else
        echo "❌ Erreur: wget ou curl requis"
        echo "   Installez d'abord: sudo apk add wget"
        exit 1
    fi
    
    if [ $? -eq 0 ]; then
        echo "✅ Téléchargé: $dest"
    else
        echo "❌ Échec téléchargement: $url"
        exit 1
    fi
}

# 1. Mettre à jour apk
echo "\n🔄 Mise à jour des paquets système..."
sudo_cmd apk update

# 2. Installer les dépendances
echo "📦 Installation des dépendances..."
sudo_cmd apk add python3 py3-pip wget curl

# 3. Télécharger zarch depuis GitHub
echo "\n🌐 Téléchargement de zarch..."
ZARCH_PATH="/usr/local/bin/zarch"
download_file "$ZARCH_URL" "$ZARCH_PATH"

# Rendre exécutable
sudo_cmd chmod +x "$ZARCH_PATH"
echo "✅ zarch rendu exécutable"

# 4. Télécharger packages.json (optionnel)
echo "\n📦 Téléchargement de la liste des paquets..."
PKG_PATH="/etc/zarch/packages.json"
sudo_cmd mkdir -p /etc/zarch
download_file "$PKG_URL" "$PKG_PATH"

# 5. Créer le dossier de cache
echo "\n📁 Configuration des dossiers..."
sudo_cmd mkdir -p /var/cache/zarch
sudo_cmd chmod 755 /var/cache/zarch

# 6. Configurer le PATH
echo "\n🔧 Configuration de l'environnement..."
BASHRC="$HOME/.bashrc"
if [ -f "$BASHRC" ]; then
    # Vérifier si déjà configuré
    if ! grep -q "zarch" "$BASHRC"; then
        echo '# Configuration zarch' >> "$BASHRC"
        echo 'export ZARCH_HOME="/etc/zarch"' >> "$BASHRC"
        echo 'export ZARCH_CACHE="/var/cache/zarch"' >> "$BASHRC"
        echo 'alias zarch-update="sudo apk update && sudo apk upgrade"' >> "$BASHRC"
        echo 'alias zarch-clean="sudo rm -rf /var/cache/zarch/*"' >> "$BASHRC"
        echo '✅ .bashrc mis à jour'
    fi
else
    echo "📄 Création de .bashrc..."
    cat > "$BASHRC" << 'EOF'
# Configuration zarch
export ZARCH_HOME="/etc/zarch"
export ZARCH_CACHE="/var/cache/zarch"
alias zarch-update="sudo apk update && sudo apk upgrade"
alias zarch-clean="sudo rm -rf /var/cache/zarch/*"
export PATH="$PATH:/usr/local/bin"
EOF
    echo '✅ .bashrc créé'
fi

# 7. Créer le fichier de configuration
echo "\n⚙️  Création de la configuration..."
sudo_cmd mkdir -p /etc/zarch
sudo_cmd cat > /etc/zarch/config.json << EOF
{
    "version": "1.0.0",
    "install_date": "$(date +%Y-%m-%d)",
    "repo_url": "https://github.com/gopu-inc/lib",
    "bin_path": "/usr/local/bin",
    "cache_path": "/var/cache/zarch"
}
EOF

# 8. Vérifier l'installation
echo "\n🔍 Vérification de l'installation..."
if [ -x "$ZARCH_PATH" ]; then
    echo "✅ zarch installé avec succès!"
    echo "📁 Emplacement: $ZARCH_PATH"
else
    echo "❌ Erreur: zarch non exécutable"
    exit 1
fi

# 9. Afficher les instructions
echo "\n========================================"
echo "🎉 INSTALLATION TERMINÉE !"
echo "========================================"
echo "\n📖 Commandes disponibles:"
echo "   zarch list          - Lister les paquets"
echo "   zarch install <pkg> - Installer un paquet"
echo "   zarch search <term> - Rechercher"
echo "   zarch info <pkg>    - Informations"
echo "   zarch update        - Mettre à jour"
echo "   zarch help          - Aide"
echo "\n🔧 Commandes système:"
echo "   zarch-update        - Mettre à jour APK"
echo "   zarch-clean         - Nettoyer le cache"
echo "\n🔄 Pour appliquer les changements:"
echo "   source ~/.bashrc"
echo "\n💡 Premier test:"
echo "   zarch list"
echo "========================================"
