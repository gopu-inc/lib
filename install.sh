#!/bin/bash

# Zarch CLI Installer
# Installation du client Zarch en C

set -e

echo "🔧 Installing Zarch CLI..."

# Vérifier les dépendances
echo "📦 Checking dependencies..."

if ! command -v gcc &> /dev/null; then
    echo "❌ GCC is required. Please install gcc first."
    echo "   Ubuntu/Debian: sudo apt-get install gcc"
    echo "   macOS: xcode-select --install"
    exit 1
fi

if ! command -v curl &> /dev/null; then
    echo "❌ curl is required. Please install curl first."
    exit 1
fi

if ! command -v jq &> /dev/null; then
    echo "📦 Installing jq..."
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        sudo apt-get install -y jq
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        brew install jq
    fi
fi

# Installer les bibliothèques nécessaires
echo "📦 Installing required libraries..."

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    sudo apt-get update
    sudo apt-get install -y libcurl4-openssl-dev libjansson-dev libssl-dev zlib1g-dev
elif [[ "$OSTYPE" == "darwin"* ]]; then
    brew install curl jansson openssl zlib
    export PKG_CONFIG_PATH="/usr/local/opt/openssl@3/lib/pkgconfig:/usr/local/opt/zlib/lib/pkgconfig"
fi

# Télécharger le source
echo "⬇️  Downloading source code..."
curl -sL -o /tmp/zarch.c https://raw.githubusercontent.com/gopu-inc/zarch-cli/main/zarch.c

# Compiler
echo "🔨 Compiling Zarch CLI..."
cd /tmp

gcc -o zarch zarch.c \
    -lcurl \
    -ljansson \
    -lcrypto \
    -lz \
    -Wall \
    -O2 \
    -D_DEFAULT_SOURCE

if [ $? -eq 0 ]; then
    echo "✅ Compilation successful!"
    
    # Installer
    sudo mv zarch /usr/local/bin/
    sudo chmod +x /usr/local/bin/zarch
    
    # Créer le répertoire de configuration
    mkdir -p ~/.zarch/cache
    
    echo ""
    echo "🎉 Zarch CLI installed successfully!"
    echo ""
    echo "Next steps:"
    echo "1. Login: zarch login <username> <password>"
    echo "2. Initialize a package: zarch init ."
    echo "3. Publish: zarch publish . user"
    echo "4. Install packages: zarch install @user/package"
    echo ""
    echo "Registry: https://zenv-hub.onrender.com"
    echo "Documentation: https://docs.zenv-hub.onrender.com"
    
else
    echo "❌ Compilation failed!"
    exit 1
fi
