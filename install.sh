#!/bin/sh
# install-zarch-hub.sh - Install zarch with hub support
set -e

echo "Installing zarch v2.0 (Zenv Hub Edition)..."

# Installer les dépendances
if ! command -v gcc >/dev/null 2>&1; then
    echo "Installing build tools..."
    sudo apk add build-base curl-dev
fi

# Vérifier libcurl
if ! apk info libcurl >/dev/null 2>&1; then
    echo "Installing curl library..."
    sudo apk add curl curl-dev
fi

# Télécharger le code source
echo "Downloading source..."
curl -s -o /tmp/zarch.c https://raw.githubusercontent.com/gopu-inc/lib/main/zarch.c

# Compiler avec support curl
echo "Compiling..."
cd /tmp
if gcc -Wall -O2 -o zarch zarch.c -lcurl 2>/dev/null; then
    echo "✅ Compiled successfully"
else
    echo "Trying without curl..."
    # Version simplifiée sans curl
    curl -s -o /tmp/zarch-simple.c https://raw.githubusercontent.com/gopu-inc/lib/main/zarch-simple.c
    gcc -Wall -O2 -o zarch zarch-simple.c
fi

# Installer
echo "Installing binary..."
sudo cp zarch /usr/local/bin/
sudo chmod +x /usr/local/bin/zarch

# Configurer PATH
echo 'export PATH="/usr/local/bin:$PATH"' >> ~/.bashrc
export PATH="/usr/local/bin:$PATH"

echo "✅ zarch v2.0 installed!"
echo "🌐 Hub: https://zenv-hub.onrender.com"
echo "📦 Usage:"
echo "  zarch list          # List local packages"
echo "  zarch hub-list      # List hub packages"
echo "  zarch install git   # Install local package"
echo "  zarch hub-status    # Check hub connection"
