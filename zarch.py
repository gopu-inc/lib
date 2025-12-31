import os
import sys
import urllib.request
import json
import subprocess
import shutil

# ============================================
# CONFIGURATION POUR iSH (Alpine Linux)
# ============================================
HOME = os.path.expanduser("~")
BIN_DIR = "/usr/local/bin"  # iSH permet l'accès à /usr/local
PKG_DIR = os.path.join(HOME, ".zarch_packages")
REPO_URL = "https://raw.githubusercontent.com/gopu-inc/lib/main/"

# Dossiers système accessibles sur iSH
SYSTEM_BIN_DIRS = ["/usr/local/bin", "/usr/bin", "/bin"]

def check_root():
    """Vérifier si on a les permissions root"""
    if os.geteuid() != 0:
        print("⚠️  Attention: Pas en mode root")
        print("   Certaines installations nécessitent sudo")
        return False
    return True

def ensure_directories():
    """Créer les dossiers nécessaires"""
    os.makedirs(PKG_DIR, exist_ok=True)
    
    # Vérifier l'accès à /usr/local/bin
    if not os.path.exists(BIN_DIR):
        try:
            os.makedirs(BIN_DIR, exist_ok=True)
            print(f"✅ Créé: {BIN_DIR}")
        except:
            print(f"⚠️  Impossible de créer {BIN_DIR}")
            # Fallback vers ~/bin
            global BIN_DIR
            BIN_DIR = os.path.join(HOME, "bin")
            os.makedirs(BIN_DIR, exist_ok=True)
    
    # Ajouter au PATH si nécessaire
    update_path()

def update_path():
    """Mettre à jour le PATH dans .bashrc"""
    bashrc = os.path.join(HOME, ".bashrc")
    path_line = f'export PATH="{BIN_DIR}:$PATH"'
    
    # Vérifier si déjà dans .bashrc
    if os.path.exists(bashrc):
        with open(bashrc, "r") as f:
            content = f.read()
        
        if path_line not in content:
            with open(bashrc, "a") as f:
                f.write(f"\n# Added by zarch\n{path_line}\n")
            print(f"✅ PATH ajouté à {bashrc}")
    else:
        with open(bashrc, "w") as f:
            f.write(f"{path_line}\n")
    
    # Mettre à jour le PATH actuel
    os.environ["PATH"] = f"{BIN_DIR}:{os.environ.get('PATH', '')}"

def fetch_packages():
    """Récupérer packages.json depuis GitHub"""
    try:
        url = f"{REPO_URL}packages.json"
        print(f"🌐 Connexion à GitHub...")
        response = urllib.request.urlopen(url, timeout=10)
        data = json.loads(response.read().decode())
        print(f"✅ {len(data.get('packages', {}))} paquets disponibles")
        return data
    except Exception as e:
        print(f"⚠️  GitHub inaccessible: {e}")
        return get_default_packages()

def get_default_packages():
    """Paquets par défaut pour iSH"""
    return {
        "packages": {
            "python3": {
                "name": "python3",
                "version": "3.9.0",
                "type": "apk",
                "description": "Python 3.9 (via apk)",
                "install": "apk add python3 py3-pip"
            },
            "wget": {
                "name": "wget",
                "version": "1.21.3",
                "type": "apk",
                "description": "Téléchargeur web",
                "install": "apk add wget"
            },
            "git": {
                "name": "git",
                "version": "2.39.0",
                "type": "apk",
                "description": "Système de contrôle de version",
                "install": "apk add git"
            },
            "curl": {
                "name": "curl",
                "version": "7.88.1",
                "type": "apk",
                "description": "Outil de transfert de données",
                "install": "apk add curl"
            },
            "nodejs": {
                "name": "nodejs",
                "version": "18.14.0",
                "type": "apk",
                "description": "Runtime JavaScript",
                "install": "apk add nodejs npm"
            },
            "nano": {
                "name": "nano",
                "version": "6.4",
                "type": "apk",
                "description": "Éditeur de texte",
                "install": "apk add nano"
            },
            "vim": {
                "name": "vim",
                "version": "9.0",
                "type": "apk",
                "description": "Éditeur de texte avancé",
                "install": "apk add vim"
            },
            "gcc": {
                "name": "gcc",
                "version": "12.2.1",
                "type": "apk",
                "description": "Compilateur C",
                "install": "apk add build-base"
            },
            "make": {
                "name": "make",
                "version": "4.3",
                "type": "apk",
                "description": "Outil de compilation",
                "install": "apk add make"
            },
            "openssh": {
                "name": "openssh",
                "version": "9.1",
                "type": "apk",
                "description": "Client SSH",
                "install": "apk add openssh-client"
            }
        }
    }

def install_apk_package(pkg_name, pkg_info):
    """Installer un paquet via apk"""
    print(f"📦 Installation via apk: {pkg_name}")
    
    if "install" in pkg_info:
        cmd = pkg_info["install"]
        print(f"⚙️  Commande: {cmd}")
        
        # Exécuter avec sudo si nécessaire
        if os.geteuid() != 0:
            cmd = f"sudo {cmd}"
        
        result = os.system(cmd)
        if result == 0:
            print(f"✅ {pkg_name} installé via apk")
            return True
        else:
            print(f"❌ Échec installation apk")
            return False
    
    # Fallback: apk add standard
    cmd = f"apk add {pkg_name}"
    if os.geteuid() != 0:
        cmd = f"sudo {cmd}"
    
    print(f"⚙️  Commande: {cmd}")
    result = os.system(cmd)
    return result == 0

def install_script_package(pkg_name, pkg_info):
    """Installer un paquet script"""
    print(f"📦 Installation script: {pkg_name}")
    
    if "urls" not in pkg_info:
        print(f"❌ Aucune URL fournie pour {pkg_name}")
        return False
    
    try:
        for url in pkg_info["urls"]:
            filename = url.split("/")[-1]
            dest_path = os.path.join(PKG_DIR, filename)
            
            print(f"📥 Téléchargement: {filename}")
            urllib.request.urlretrieve(url, dest_path)
            
            # Si c'est un script, le rendre exécutable
            if filename.endswith((".py", ".sh", "")):
                # Copier dans /usr/local/bin
                bin_path = os.path.join(BIN_DIR, pkg_name)
                shutil.copy2(dest_path, bin_path)
                
                # Rendre exécutable
                os.chmod(bin_path, 0o755)
                print(f"✅ Script installé: {bin_path}")
            
            else:
                print(f"📦 Fichier téléchargé: {dest_path}")
        
        return True
        
    except Exception as e:
        print(f"❌ Erreur installation: {e}")
        return False

def install_package(pkg_name):
    """Installer un paquet"""
    print(f"\n🔧 INSTALLATION: {pkg_name}")
    print("=" * 50)
    
    packages_data = fetch_packages()
    
    if pkg_name not in packages_data.get("packages", {}):
        print(f"❌ Paquet '{pkg_name}' introuvable")
        
        # Suggestion: peut-être c'est un paquet apk direct
        print(f"💡 Essayez peut-être: apk add {pkg_name}")
        return False
    
    pkg = packages_data["packages"][pkg_name]
    print(f"📝 {pkg.get('description', 'Pas de description')}")
    print(f"🔖 Version: {pkg.get('version', 'N/A')}")
    print(f"📦 Type: {pkg.get('type', 'apk')}")
    
    # Vérifier si déjà installé
    if shutil.which(pkg_name):
        print(f"✅ {pkg_name} est déjà installé")
        return True
    
    # Installation selon le type
    pkg_type = pkg.get("type", "apk")
    
    if pkg_type == "apk":
        return install_apk_package(pkg_name, pkg)
    elif pkg_type in ["script", "binary"]:
        return install_script_package(pkg_name, pkg)
    else:
        print(f"❌ Type inconnu: {pkg_type}")
        return False

def list_packages():
    """Afficher tous les paquets disponibles"""
    packages_data = fetch_packages()
    
    print("\n" + "=" * 70)
    print("📦 PAQUETS DISPONIBLES (iSH Alpine Linux)")
    print("=" * 70)
    
    packages = packages_data.get("packages", {})
    if not packages:
        print("❌ Aucun paquet trouvé")
        return
    
    print(f"{'Nom':<15} {'Version':<12} {'Type':<8} Description")
    print("-" * 70)
    
    for name, pkg in packages.items():
        version = pkg.get("version", "N/A")
        pkg_type = pkg.get("type", "apk")
        desc = pkg.get("description", "")[:50]
        print(f"{name:<15} {version:<12} {pkg_type:<8} {desc}")
    
    print("=" * 70)
    print(f"💡 Installer: python3 {sys.argv[0]} install <nom>")
    print(f"💡 Ou directement: apk add <nom>")

def search_packages(keyword):
    """Rechercher des paquets"""
    packages_data = fetch_packages()
    
    print(f"\n🔍 RECHERCHE: '{keyword}'")
    print("=" * 50)
    
    found = False
    for name, pkg in packages_data.get("packages", {}).items():
        if (keyword.lower() in name.lower() or 
            keyword.lower() in pkg.get("description", "").lower()):
            version = pkg.get("version", "N/A")
            desc = pkg.get("description", "")
            print(f"  • {name} v{version} - {desc}")
            found = True
    
    if not found:
        print("❌ Aucun paquet trouvé")

def update_system():
    """Mettre à jour le système iSH"""
    print("\n🔄 MISE À JOUR SYSTÈME")
    print("=" * 50)
    
    if os.geteuid() != 0:
        print("⚠️  Nécessite les droits root")
        print("💡 Utilisez: sudo python3 zarch.py update")
        return False
    
    cmds = [
        "apk update",
        "apk upgrade",
        "apk add --upgrade apk-tools"
    ]
    
    for cmd in cmds:
        print(f"⚙️  {cmd}")
        result = os.system(cmd)
        if result != 0:
            print(f"❌ Erreur: {cmd}")
            return False
    
    print("✅ Système mis à jour")
    return True

def cleanup():
    """Nettoyer le cache"""
    print("\n🧹 NETTOYAGE CACHE")
    print("=" * 50)
    
    if os.path.exists(PKG_DIR):
        shutil.rmtree(PKG_DIR)
        os.makedirs(PKG_DIR)
        print(f"✅ Cache nettoyé: {PKG_DIR}")
    
    # Nettoyer le cache apk
    if os.geteuid() == 0:
        os.system("apk cache clean")
        print("✅ Cache APK nettoyé")

def show_info(pkg_name):
    """Afficher les infos détaillées d'un paquet"""
    packages_data = fetch_packages()
    
    if pkg_name not in packages_data.get("packages", {}):
        print(f"❌ Paquet '{pkg_name}' introuvable")
        return
    
    pkg = packages_data["packages"][pkg_name]
    
    print(f"\n📋 INFORMATIONS: {pkg_name}")
    print("=" * 50)
    print(f"Nom:        {pkg.get('name', pkg_name)}")
    print(f"Version:    {pkg.get('version', 'N/A')}")
    print(f"Type:       {pkg.get('type', 'apk')}")
    print(f"Description: {pkg.get('description', '')}")
    
    if "urls" in pkg:
        print("\n📥 URLs:")
        for url in pkg["urls"]:
            print(f"  • {url}")
    
    if "install" in pkg:
        print(f"\n⚙️  Commande d'installation:")
        print(f"  {pkg['install']}")
    
    # Vérifier si installé
    if shutil.which(pkg_name):
        print(f"\n✅ Statut: Installé")
    else:
        print(f"\n❌ Statut: Non installé")

def main():
    print("🐧 ZARCH - Gestionnaire de paquets iSH")
    print("=" * 50)
    print(f"📁 Bin: {BIN_DIR}")
    print(f"🌐 Dépôt: {REPO_URL}")
    print(f"👤 Root: {'✅ Oui' if os.geteuid() == 0 else '❌ Non'}")
    print()
    
    ensure_directories()
    
    if len(sys.argv) < 2:
        list_packages()
        return
    
    command = sys.argv[1].lower()
    
    if command == "install" and len(sys.argv) >= 3:
        results = []
        for pkg in sys.argv[2:]:
            if install_package(pkg):
                results.append(f"✅ {pkg}")
            else:
                results.append(f"❌ {pkg}")
        
        print("\n" + "=" * 50)
        print("📊 RÉSULTATS:")
        for result in results:
            print(f"  {result}")
        
        print(f"\n💡 Pour les paquets apk, utilisez aussi:")
        print(f"   apk add <nom-du-paquet>")
    
    elif command == "list":
        list_packages()
    
    elif command == "search" and len(sys.argv) >= 3:
        search_packages(sys.argv[2])
    
    elif command == "info" and len(sys.argv) >= 3:
        show_info(sys.argv[2])
    
    elif command == "update":
        update_system()
    
    elif command == "upgrade":
        # Alias pour update
        update_system()
    
    elif command == "clean":
        cleanup()
    
    elif command == "setup":
        ensure_directories()
        print(f"\n✅ Configuration terminée!")
        print(f"📁 Bin directory: {BIN_DIR}")
        print(f"📁 Cache directory: {PKG_DIR}")
        print(f"🔧 .bashrc mis à jour")
        print(f"\n🔄 Redémarrez le terminal ou faites:")
        print(f"   source ~/.bashrc")
    
    elif command == "installed":
        print("\n📦 PAQUETS INSTALLÉS (dans PATH):")
        print("=" * 50)
        
        for bin_dir in SYSTEM_BIN_DIRS + [BIN_DIR]:
            if os.path.exists(bin_dir):
                print(f"\n📁 {bin_dir}/")
                try:
                    files = sorted(os.listdir(bin_dir))
                    for f in files:
                        path = os.path.join(bin_dir, f)
                        if os.path.isfile(path) and os.access(path, os.X_OK):
                            print(f"  • {f}")
                except:
                    pass
    
    else:
        print("❌ Commande inconnue")
        print("\n📖 Commandes disponibles:")
        print("  install <pkg...>  - Installer des paquets")
        print("  list              - Lister les paquets")
        print("  search <term>     - Rechercher un paquet")
        print("  info <pkg>        - Informations détaillées")
        print("  update/upgrade    - Mettre à jour le système")
        print("  clean             - Nettoyer le cache")
        print("  setup             - Configurer l'environnement")
        print("  installed         - Voir les paquets installés")
        print("\n💡 Sur iSH, vous pouvez aussi utiliser:")
        print("   apk add <paquet>   - Installer directement")
        print("   apk search <term>  - Rechercher dans apk")

if __name__ == "__main__":
    main()
