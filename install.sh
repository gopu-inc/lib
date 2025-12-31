#!/bin/sh
# install.sh - Installateur pour zarch (version C)
set -e

echo "Installing zarch (C version)..."

# Installer gcc si besoin
if ! command -v gcc >/dev/null 2>&1; then
    echo "Installing build tools..."
    sudo apk add build-base
fi

# Télécharger et compiler
echo "Downloading source..."
curl -s -o /tmp/zarch.c https://raw.githubusercontent.com/gopu-inc/lib/main/zarch.c

echo "Compiling..."
cd /tmp
gcc -Wall -O2 -o zarch zarch.c

echo "Installing binary..."
sudo cp zarch /usr/local/bin/
sudo chmod +x /usr/local/bin/zarch

# Configurer PATH
echo 'export PATH="/usr/local/bin:$PATH"' >> ~/.bashrc
export PATH="/usr/local/bin:$PATH"

echo "✅ zarch installed!"
echo "Usage: zarch list"
