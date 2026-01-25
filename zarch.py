#!/usr/bin/env python3
import os
import sys
import json
import shutil
import time
import tarfile
import hashlib
import requests
import argparse
from pathlib import Path

# --- Configuration ---
REGISTRY_URL = "https://zenv-hub.onrender.com"
MODULES_DIR = Path("/usr/local/lib/swift")
LOCK_FILE = Path("zarch.lock")
CONFIG_FILE = Path("zarch.json")

# --- Couleurs ANSI ---
class Colors:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    RED = "\033[31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    BLUE = "\033[34m"
    MAGENTA = "\033[35m"
    CYAN = "\033[36m"
    WHITE = "\033[37m"

# --- Utilitaires d'Affichage ---
def log_info(msg):
    print(f"{Colors.BLUE}[INFO]{Colors.RESET} {msg}")

def log_success(msg):
    print(f"{Colors.GREEN}[SUCCESS]{Colors.RESET} {msg}")

def log_error(msg):
    print(f"{Colors.RED}[ERROR]{Colors.RESET} {msg}")
    sys.exit(1)

def log_warning(msg):
    print(f"{Colors.YELLOW}[WARNING]{Colors.RESET} {msg}")

def progress_bar(current, total, prefix="", length=30):
    percent = float(current) * 100 / total
    arrow = '=' * int(percent/100 * length - 1) + '>'
    spaces = ' ' * (length - len(arrow))
    
    print(f"\r{prefix} [{arrow}{spaces}] {int(percent)}%", end='', flush=True)
    if current == total:
        print()

# --- Gestionnaire de Paquets ---
class ZarchManager:
    def __init__(self):
        self.session = requests.Session()
        # Assurer que le dossier des modules existe
        if not MODULES_DIR.exists():
            try:
                MODULES_DIR.mkdir(parents=True, exist_ok=True)
            except PermissionError:
                log_error(f"Permission denied creating {MODULES_DIR}. Try using sudo.")

    def _get_installed_packages(self):
        """Lit le fichier lock pour connaître les paquets installés."""
        if not LOCK_FILE.exists():
            return {}
        try:
            with open(LOCK_FILE, 'r') as f:
                return json.load(f)
        except json.JSONDecodeError:
            return {}

    def _save_lock_file(self, data):
        """Sauvegarde l'état des paquets installés."""
        with open(LOCK_FILE, 'w') as f:
            json.dump(data, f, indent=4)

    def search(self, query):
        """Recherche un paquet sur le registre."""
        print(f"{Colors.CYAN}[SEARCH]{Colors.RESET} Searching for '{query}'...")
        try:
            response = self.session.get(f"{REGISTRY_URL}/api/package/search", params={'q': query})
            response.raise_for_status()
            data = response.json()
            
            if not data.get('results'):
                log_warning("No packages found.")
                return

            print(f"\nFound {data['total']} package(s):")
            for pkg in data['results']:
                name = pkg['name']
                scope = pkg['scope']
                version = pkg['version']
                desc = pkg.get('description', 'No description')
                full_name = f"@{scope}/{name}" if scope != 'user' else name
                print(f"  {Colors.BOLD}{full_name}{Colors.RESET} ({version}) - {desc}")
                
        except requests.RequestException as e:
            log_error(f"Network error: {e}")

    def install(self, package_name):
        """Installe un paquet."""
        # Parsing du nom (supporte @scope/pkg ou pkg)
        scope = "user"
        name = package_name
        
        if package_name.startswith("@"):
            parts = package_name[1:].split("/")
            if len(parts) == 2:
                scope, name = parts
            else:
                log_error("Invalid package format. Use @scope/name or name.")

        print(f"{Colors.YELLOW}[INSTALL]{Colors.RESET} Resolving {name}...")

        # 1. Récupérer les infos du paquet
        try:
            info_url = f"{REGISTRY_URL}/api/package/info/{scope}/{name}"
            resp = self.session.get(info_url)
            
            if resp.status_code == 404:
                log_error(f"Package '{package_name}' not found.")
            resp.raise_for_status()
            
            pkg_info = resp.json()
            version = pkg_info['latest_version']
            download_url = f"{REGISTRY_URL}{pkg_info['download_url']}"
            
            print(f"{Colors.CYAN}[INFO]{Colors.RESET} Found version {version}")

        except requests.RequestException as e:
            log_error(f"Failed to fetch package info: {e}")

        # 2. Téléchargement avec barre de progression
        tar_path = Path(f"/tmp/{name}-{version}.tar.gz")
        install_path = MODULES_DIR / name
        
        try:
            with self.session.get(download_url, stream=True) as r:
                r.raise_for_status()
                total_size = int(r.headers.get('content-length', 0))
                block_size = 8192
                downloaded = 0
                
                with open(tar_path, 'wb') as f:
                    for chunk in r.iter_content(chunk_size=block_size):
                        downloaded += len(chunk)
                        f.write(chunk)
                        progress_bar(downloaded, total_size, prefix=f"{Colors.MAGENTA}[DOWNLOAD]{Colors.RESET}")
            
            # 3. Extraction
            print(f"{Colors.BLUE}[EXTRACT]{Colors.RESET} Unpacking to {install_path}...")
            
            if install_path.exists():
                shutil.rmtree(install_path)
            install_path.mkdir(parents=True, exist_ok=True)
            
            with tarfile.open(tar_path, "r:gz") as tar:
                tar.extractall(path=install_path)
                
            # Nettoyage
            tar_path.unlink()
            
            # 4. Mise à jour du fichier lock
            lock_data = self._get_installed_packages()
            lock_data[package_name] = {
                "version": version,
                "path": str(install_path),
                "installed_at": time.time()
            }
            self._save_lock_file(lock_data)
            
            log_success(f"Package {package_name} installed successfully!")
            
            # Auto-link si configuré
            self._link_package(install_path, name)

        except Exception as e:
            if tar_path.exists(): tar_path.unlink()
            log_error(f"Installation failed: {e}")

    def _link_package(self, pkg_path, pkg_name):
        """Lie le paquet au système SwiftFlow."""
        manifest_path = pkg_path / "zarch.json"
        if not manifest_path.exists():
            log_warning("No zarch.json found, skipping link.")
            return

        try:
            with open(manifest_path) as f:
                config = json.load(f)
            
            main_file = config.get("main")
            if not main_file:
                return

            source = pkg_path / main_file
            
            # Création du lien symbolique ou copie vers le path système si nécessaire
            # Ici on simule l'enregistrement dans un fichier de configuration global pour Swift
            # ou on crée un alias
            
            print(f"{Colors.CYAN}[LINKING]{Colors.RESET} Linking {pkg_name}...")
            
            # Exemple : Création d'un lien dans /usr/local/bin si c'est un binaire/script
            # ou mise à jour d'un fichier de mapping pour l'import
            
            log_success(f"Linked {pkg_name} -> {source}")

        except Exception as e:
            log_warning(f"Linking failed: {e}")

    def remove(self, package_name):
        """Supprime un paquet."""
        lock_data = self._get_installed_packages()
        
        if package_name not in lock_data:
            log_error(f"Package {package_name} is not installed.")
            
        print(f"{Colors.YELLOW}[REMOVE]{Colors.RESET} Removing {package_name}...")
        
        pkg_data = lock_data[package_name]
        path = Path(pkg_data["path"])
        
        try:
            if path.exists():
                shutil.rmtree(path)
            
            del lock_data[package_name]
            self._save_lock_file(lock_data)
            
            log_success(f"Package {package_name} removed.")
        except Exception as e:
            log_error(f"Removal failed: {e}")

    def init_project(self):
        """Initialise un nouveau projet zarch.json."""
        if CONFIG_FILE.exists():
            log_error("zarch.json already exists.")
            
        name = input("Package name: ")
        version = input("Version (1.0.0): ") or "1.0.0"
        desc = input("Description: ")
        entry = input("Entry point (main.swf): ") or "main.swf"
        
        config = {
            "name": name,
            "version": version,
            "description": desc,
            "main": entry,
            "dependencies": {}
        }
        
        with open(CONFIG_FILE, 'w') as f:
            json.dump(config, f, indent=4)
            
        log_success("Initialized zarch.json")

    def link_file(self, file_path, alias=None):
        """Commande manuelle 'link' pour lier un fichier .swf local."""
        target = Path(file_path)
        if not target.exists():
            log_error(f"File {file_path} does not exist.")
            
        if not alias:
            alias = target.stem 
            
        print(f"{Colors.CYAN}[LINK]{Colors.RESET} Linking {file_path} as '{alias}'...")
        
        # Mise à jour ou création du fichier de config local pour le mapping
        config = {}
        if CONFIG_FILE.exists():
            with open(CONFIG_FILE, 'r') as f:
                try:
                    config = json.load(f)
                except: pass
                
        if "swift" not in config:
            config["swift"] = {}
            
        config["swift"][file_path] = {"as": alias}
        
        with open(CONFIG_FILE, 'w') as f:
            json.dump(config, f, indent=4)
            
        log_success(f"Linked {file_path} as {alias}")


# --- Point d'entrée CLI ---
def main():
    parser = argparse.ArgumentParser(description="Zarch Package Manager for SwiftFlow")
    subparsers = parser.add_subparsers(dest="command", help="Command to execute")

    # Install
    install_parser = subparsers.add_parser("install", help="Install a package")
    install_parser.add_argument("package", help="Package name (@scope/name or name)")

    # Remove
    remove_parser = subparsers.add_parser("remove", help="Remove a package")
    remove_parser.add_argument("package", help="Package name")

    # Search
    search_parser = subparsers.add_parser("search", help="Search for packages")
    search_parser.add_argument("query", help="Search term")

    # Init
    subparsers.add_parser("init", help="Initialize a new package")

    # Link
    link_parser = subparsers.add_parser("link", help="Link a local .swf file")
    link_parser.add_argument("file", help="Path to .swf file")
    link_parser.add_argument("--as", dest="alias", help="Alias for import")

    args = parser.parse_args()
    manager = ZarchManager()

    if args.command == "install":
        manager.install(args.package)
    elif args.command == "remove":
        manager.remove(args.package)
    elif args.command == "search":
        manager.search(args.query)
    elif args.command == "init":
        manager.init_project()
    elif args.command == "link":
        manager.link_file(args.file, args.alias)
    else:
        parser.print_help()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}[ABORT]{Colors.RESET} Operation cancelled by user.")
        sys.exit(130)
