#!/bin/sh
echo "Fichiers présents:"
ls -la

echo ""
echo "Pattern *:"
ls *

echo ""
echo "Fichiers spécifiques:"
[ -f "sqt.h" ] && echo "✓ sqt.h existe"
[ -f "sqt.c" ] && echo "✓ sqt.c existe"
[ -f "example.c" ] && echo "✓ example.c existe"
[ -f "Makefile" ] && echo "✓ Makefile existe"
[ -f "README" ] && echo "✓ README existe"
[ -f "README.md" ] && echo "✓ README.md existe"
