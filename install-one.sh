#!/bin/sh
# One-liner installer - à mettre dans le README
# Usage: curl -sSL https://raw.githubusercontent.com/gopu-inc/lib/main/install.sh | sudo sh

echo "Installing zarch..."
wget -q -O /tmp/zarch-installer.sh https://raw.githubusercontent.com/gopu-inc/lib/main/install-zarch.sh
chmod +x /tmp/zarch-installer.sh
sudo /tmp/zarch-installer.sh
