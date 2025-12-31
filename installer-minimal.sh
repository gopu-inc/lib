#!/bin/sh
# Installateur minimal de zarch - sans wget/curl
# Utilise Python pour télécharger

set -e

echo "========================================"
echo "🐧 INSTALLATEUR ZARCH (version minimale)"
echo "========================================"

# Vérifier Python
if ! command -v python3 >/dev/null 2>&1; then
    echo "❌ Python3 n'est pas installé"
    echo "📦 Installation de Python3..."
    apk add python3 || {
        echo "❌ Impossible d'installer Python3"
        exit 1
    }
fi

# URL du dépôt
REPO="https://raw.githubusercontent.com/gopu-inc/lib/main"

# Fonction pour télécharger avec Python
download_with_python() {
    local url="$1"
    local dest="$2"
    
    python3 -c "
import urllib.request
import sys
try:
    print('📥 Téléchargement:', sys.argv[1].split('/')[-1])
    urllib.request.urlretrieve(sys.argv[1], sys.argv[2])
    print('✅ Fichier téléchargé:', sys.argv[2])
except Exception as e:
    print('❌ Erreur:', e)
    sys.exit(1)
" "$url" "$dest"
}

# 1. Télécharger zarch
echo "\n🌐 Téléchargement de zarch..."
ZARCH_PATH="/usr/local/bin/zarch"
download_with_python "$REPO/zarch" "/tmp/zarch"

# 2. Télécharger packages.json
echo "\n📦 Téléchargement de la liste des paquets..."
download_with_python "$REPO/packages.json" "/tmp/packages.json"

# 3. Installer zarch
echo "\n⚙️  Installation..."
if [ -f "/tmp/zarch" ]; then
    # Vérifier que c'est un script Python valide
    if head -1 "/tmp/zarch" | grep -q "python"; then
        # Copier avec sudo si nécessaire
        if [ "$(id -u)" -eq 0 ]; then
            cp "/tmp/zarch" "$ZARCH_PATH"
            chmod +x "$ZARCH_PATH"
        else
            sudo cp "/tmp/zarch" "$ZARCH_PATH"
            sudo chmod +x "$ZARCH_PATH"
        fi
        echo "✅ zarch installé dans $ZARCH_PATH"
    else
        echo "❌ Le fichier zarch n'est pas un script Python valide"
        exit 1
    fi
else
    echo "❌ Fichier zarch non téléchargé"
    exit 1
fi

# 4. Installer packages.json
echo "\n📁 Configuration..."
ZARCH_DIR="/etc/zarch"
if [ "$(id -u)" -eq 0 ]; then
    mkdir -p "$ZARCH_DIR"
    cp "/tmp/packages.json" "$ZARCH_DIR/"
    mkdir -p "/var/cache/zarch"
    chmod 755 "/var/cache/zarch"
else
    sudo mkdir -p "$ZARCH_DIR"
    sudo cp "/tmp/packages.json" "$ZARCH_DIR/"
    sudo mkdir -p "/var/cache/zarch"
    sudo chmod 755 "/var/cache/zarch"
fi
echo "✅ Configuration installée dans $ZARCH_DIR"

# 5. Configurer le PATH
echo "\n🔧 Configuration du PATH..."
BASHRC="$HOME/.bashrc"
PATH_LINE='export PATH="/usr/local/bin:$PATH"'

if [ -f "$BASHRC" ]; then
    if ! grep -q "/usr/local/bin" "$BASHRC"; then
        echo "$PATH_LINE" >> "$BASHRC"
        echo "✅ PATH ajouté à $BASHRC"
    else
        echo "✅ PATH déjà configuré"
    fi
else
    echo "$PATH_LINE" > "$BASHRC"
    echo "✅ $BASHRC créé avec PATH"
fi

# 6. Créer un alias pratique
echo "\n🔗 Création d'alias..."
if [ -f "$BASHRC" ]; then
    if ! grep -q "alias zarch-update" "$BASHRC"; then
        echo '' >> "$BASHRC"
        echo '# Alias zarch' >> "$BASHRC"
        echo 'alias zarch-update="sudo apk update && sudo apk upgrade"' >> "$BASHRC"
        echo 'alias zarch-clean="sudo rm -rf /var/cache/zarch/*"' >> "$BASHRC"
        echo '✅ Alias créés'
    fi
fi

# 7. Test final
echo "\n🔍 Test final..."
if command -v zarch >/dev/null 2>&1; then
    echo "✅ zarch est maintenant disponible !"
    echo ""
    echo "========================================"
    echo "🎉 INSTALLATION RÉUSSIE !"
    echo "========================================"
    echo ""
    echo "📖 Pour commencer:"
    echo "   zarch list          # Lister les paquets"
    echo "   zarch help          # Afficher l'aide"
    echo ""
    echo "🔄 Pour appliquer les changements:"
    echo "   source ~/.bashrc"
    echo "   ou redémarrez le terminal"
    echo "========================================"
else
    echo "❌ zarch n'est pas dans le PATH"
    echo "💡 Essayez: source ~/.bashrc"
    echo "💡 Ou: export PATH=\"/usr/local/bin:\$PATH\""
fi

# Nettoyer
rm -f "/tmp/zarch" "/tmp/packages.json"
