#!/usr/bin/env python3
import os
import sys
import json
import shutil
import time
import tarfile
import requests
import argparse
import glob
import hashlib
from pathlib import Path

# ==============================================================================
# CONFIGURATION
# ==============================================================================
REGISTRY_URL = "https://zenv-hub.onrender.com"
MODULES_DIR = Path("/usr/local/lib/swift")
USER_CONFIG_DIR = Path.home() / ".zarch"
CREDENTIALS_FILE = USER_CONFIG_DIR / "credentials.json"

# Noms de fichiers standards
LOCK_FILE_NAME = "zarch.lock.json"
CONFIG_FILE_NAME = "zarch.json"
DEP_FILE_NAME = "SwiftList.txt"

# ==============================================================================
# STYLE & UTILITAIRES
# ==============================================================================
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

def log_info(msg): print(f"{Colors.BLUE}[INFO]{Colors.RESET} {msg}")
def log_success(msg): print(f"{Colors.GREEN}[SUCCESS]{Colors.RESET} {msg}")
def log_error(msg): print(f"{Colors.RED}[ERROR]{Colors.RESET} {msg}"); sys.exit(1)
def log_warn(msg): print(f"{Colors.YELLOW}[WARN]{Colors.RESET} {msg}")
def log_step(step, msg): print(f"{Colors.MAGENTA}[{step}]{Colors.RESET} {msg}")

def progress_bar(current, total, prefix="", length=40):
    """Affiche une barre de progression textuelle"""
    if total <= 0: total = 1
    percent = float(current) * 100 / total
    if percent > 100: percent = 100
    
    filled = int(length * current // total)
    if filled > length: filled = length
    
    bar = '=' * filled + '-' * (length - filled)
    print(f"\r{prefix} [{bar}] {percent:.1f}%", end='', flush=True)
    if current >= total: print()

def calculate_file_hash(filepath):
    """Calcule le hash SHA256 d'un fichier"""
    sha256 = hashlib.sha256()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            sha256.update(chunk)
    return sha256.hexdigest()

# ==============================================================================
# GESTIONNAIRE ZARCH
# ==============================================================================
class ZarchManager:
    def __init__(self):
        self.session = requests.Session()
        
        # Configuration Utilisateur
        if not USER_CONFIG_DIR.exists():
            try: USER_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
            except: pass
        
        # Dossier Système (Modules)
        if not MODULES_DIR.exists():
            try: 
                MODULES_DIR.mkdir(parents=True, exist_ok=True)
            except PermissionError: 
                # On ne bloque pas ici, l'erreur surviendra à l'installation si pas sudo
                pass

    # --- AUTHENTIFICATION ---
    
    def login(self, username, password):
        log_step("AUTH", f"Authenticating as {username}...")
        try:
            resp = self.session.post(f"{REGISTRY_URL}/api/auth/login", json={"username": username, "password": password})
            if resp.status_code == 200:
                data = resp.json()
                creds = {
                    "username": username, 
                    "token": data.get("token"), 
                    "login_time": time.time()
                }
                with open(CREDENTIALS_FILE, 'w') as f: json.dump(creds, f)
                log_success("Logged in successfully.")
            else:
                try: err = resp.json().get('error', 'Unknown')
                except: err = resp.text
                log_error(f"Login failed: {err}")
        except Exception as e: log_error(f"Connection error: {e}")

    def logout(self):
        if CREDENTIALS_FILE.exists():
            CREDENTIALS_FILE.unlink()
            log_success("Logged out. Local credentials removed.")
        else:
            log_warn("No active session found.")

    def _get_token(self):
        if not CREDENTIALS_FILE.exists(): return None
        try:
            with open(CREDENTIALS_FILE) as f: return json.load(f).get("token")
        except: return None

    # --- INITIALISATION PROJET ---

    def init_project(self, name):
        if not name: 
            name = input(f"{Colors.YELLOW}Project name: {Colors.RESET}").strip()
        if not name: log_error("Project name required.")
        
        root = Path(name)
        if root.exists(): log_error(f"Directory '{name}' already exists.")
        
        log_step("INIT", f"Initializing project structure for '{name}'...")
        
        try:
            root.mkdir()
            (root / "src").mkdir()
            (root / "tests").mkdir()
            (root / "dist").mkdir()
        except Exception as e: log_error(f"Filesystem error: {e}")

        # 1. zarch.json
        config = {
            "name": name,
            "version": "1.0.0",
            "description": "A SwiftFlow package",
            "main": "src/main.swf",
            "author": "",
            "license": "MIT",
            "scope": "user",
            "build": "all",
            "markdown": [] # Sera rempli par README.md au build
        }
        with open(root / CONFIG_FILE_NAME, 'w') as f: json.dump(config, f, indent=4)
        
        # 2. README.md
        with open(root / "README.md", 'w') as f:
            f.write(f"# {name}\n\nCreated with Zarch Package Manager.")

        # 3. SwiftList.txt
        with open(root / DEP_FILE_NAME, 'w') as f:
            f.write("# SwiftFlow Dependencies List\n# Format: @scope/package version\n")

        # 4. Source
        with open(root / "src/main.swf", 'w') as f:
            f.write(f'// Package {name}\nprint("Hello from {name}");\n')

        log_success(f"Project initialized in ./{name}")

    # --- BUILD ---

    def build_package(self):
        if not Path(CONFIG_FILE_NAME).exists(): 
            log_error(f"No {CONFIG_FILE_NAME} found. Are you in the project root?")
        
        # 1. Lire la config
        with open(CONFIG_FILE_NAME) as f: config = json.load(f)
        name = config.get("name")
        version = config.get("version")
        
        log_step("BUILD", f"Compiling package {name} v{version}...")
        
        # 2. Injecter le README dans le JSON (en mémoire pour l'archive)
        if os.path.exists("README.md"):
            try:
                with open("README.md", "r") as f:
                    config["markdown"] = f.read().splitlines()
                log_info("Included README.md in package manifest.")
            except: pass
        
        # 3. Nettoyer et créer DIST
        dist_dir = Path("dist")
        if dist_dir.exists(): shutil.rmtree(dist_dir)
        dist_dir.mkdir()
        
        # 4. Créer l'archive
        tar_name = f"{name}-v{version}.tar.gz"
        tar_path = dist_dir / tar_name
        
        with tarfile.open(tar_path, "w:gz") as tar:
            for root, dirs, files in os.walk("."):
                # Ignorer dossiers systèmes et dist
                if "dist" in root or ".git" in root or "__pycache__" in root: continue
                
                for file in files:
                    full_path = os.path.join(root, file)
                    rel_path = os.path.relpath(full_path, ".")
                    
                    if rel_path == tar_name: continue
                    
                    # Si c'est zarch.json, on utilise notre version en mémoire (avec markdown)
                    if file == CONFIG_FILE_NAME:
                        # On écrit un fichier temporaire
                        with open(".temp_zarch.json", "w") as tf:
                            json.dump(config, tf, indent=4)
                        tar.add(".temp_zarch.json", arcname=CONFIG_FILE_NAME)
                        os.remove(".temp_zarch.json")
                    else:
                        tar.add(full_path, arcname=rel_path)

        # 5. Lockfile
        config["built_at"] = time.time()
        config["hash"] = calculate_file_hash(tar_path)
        with open(LOCK_FILE_NAME, "w") as f: json.dump(config, f, indent=4)
        
        size_kb = tar_path.stat().st_size / 1024
        log_success(f"Package built: dist/{tar_name} ({size_kb:.2f} KB)")

    # --- PUBLISH ---

    def publish_package(self, file_pattern=None):
        token = self._get_token()
        if not token: log_error("Login required to publish. Use 'zarch login'.")
        
        if not Path(CONFIG_FILE_NAME).exists(): log_error("No zarch.json found.")
        with open(CONFIG_FILE_NAME) as f: config = json.load(f)
        
        # Auto-Build si nécessaire
        target_file = file_pattern
        if not target_file:
            expected = f"dist/{config['name']}-v{config['version']}.tar.gz"
            if not os.path.exists(expected):
                log_warn("Dist not found. Auto-building...")
                self.build_package()
            target_file = expected

        if not os.path.exists(target_file): log_error("Package file missing.")
        
        scope = config.get("scope", "user")
        name = config.get("name")
        
        log_step("PUBLISH", f"Uploading {name} to registry...")
        
        url = f"{REGISTRY_URL}/api/package/upload/{scope}/{name}"
        
        try:
            with open(target_file, 'rb') as f:
                headers = {'Authorization': f'Bearer {token}'}
                files = {'file': (os.path.basename(target_file), f, 'application/gzip')}
                data = {
                    'version': config['version'],
                    'description': config.get('description', ''),
                    'license': config.get('license', 'MIT')
                }
                
                print(f"{Colors.CYAN}[UPLOADING]{Colors.RESET} Transmitting data...", end="\r")
                
                r = self.session.post(url, headers=headers, files=files, data=data)
                
                if r.status_code == 200:
                    print(" " * 30, end="\r") # Clean line
                    log_success("Published successfully!")
                    print(f"  URL: {REGISTRY_URL}/package/{scope}/{name}")
                else:
                    try: err = r.json().get('error', r.text)
                    except: err = r.text
                    log_error(f"Server rejected upload: {err}")
                    
        except Exception as e: log_error(f"Network error: {e}")

    # --- INSTALL (SMART) ---

    def install(self, package_spec=None):
        # MODE 1: Install from SwiftList.txt
        if not package_spec:
            if not Path(DEP_FILE_NAME).exists():
                log_error(f"No {DEP_FILE_NAME} found. Provide a package name to install.")
            
            log_step("INSTALL", f"Reading dependencies from {DEP_FILE_NAME}...")
            dependencies = {}
            
            with open(DEP_FILE_NAME, 'r') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('#'): continue
                    parts = line.split()
                    pkg = parts[0]
                    ver = parts[1] if len(parts) > 1 else "latest"
                    dependencies[pkg] = ver
            
            if not dependencies:
                log_warn("No dependencies found in list.")
                return

            self._batch_install(dependencies)
            return

        # MODE 2: Install single package
        log_step("INSTALL", f"Resolving {package_spec}...")
        
        # Parsing @scope/name
        if package_spec.startswith("@"):
            try:
                scope, name = package_spec[1:].split("/")
            except:
                log_error("Invalid format. Use @scope/name")
        else:
            scope, name = "user", package_spec

        # Fetch info
        try:
            r = self.session.get(f"{REGISTRY_URL}/api/package/info/{scope}/{name}")
            if r.status_code != 200: log_error("Package not found on registry.")
            
            info = r.json()
            version = info['latest_version']
            dl_url = f"{REGISTRY_URL}{info['download_url']}"
            size = info.get('size', 0)
            
            log_info(f"Found {name} v{version}")
            
            # Download & Install
            self._download_and_extract(scope, name, version, dl_url, size)
            
            # Add to SwiftList if success
            self._add_to_swiftlist(f"@{scope}/{name}", version)
            
        except Exception as e: log_error(f"Installation error: {e}")

    def _batch_install(self, deps):
        """Utilise l'API batch-resolve pour la vitesse"""
        log_step("RESOLVE", f"Batch resolving {len(deps)} packages...")
        
        try:
            r = self.session.post(f"{REGISTRY_URL}/api/package/batch-resolve", json={"dependencies": deps})
            if r.status_code != 200: log_error("Server error during resolution.")
            
            resolved = r.json().get("resolved", {})
            
            for pkg_key, info in resolved.items():
                if "error" in info:
                    log_error(f"Could not resolve {pkg_key}: {info['error']}")
                    continue
                
                # Parse scope/name from key
                if pkg_key.startswith("@"):
                    scope, name = pkg_key[1:].split("/")
                else:
                    scope, name = "user", pkg_key
                
                print(f"{Colors.CYAN}[PKG]{Colors.RESET} {pkg_key} -> {info['version']}")
                self._download_and_extract(scope, name, info['version'], info['url'], 0)
                
        except Exception as e: log_error(f"Batch failed: {e}")

    def _download_and_extract(self, scope, name, version, url, size):
        if not url.startswith("http"): url = REGISTRY_URL + url
        
        tmp = f"/tmp/{name}-{version}.tar.gz"
        target_dir = MODULES_DIR / name
        
        # Download
        try:
            with self.session.get(url, stream=True) as r:
                if r.status_code != 200: log_error(f"Download error {r.status_code}")
                total = int(r.headers.get('content-length', size))
                dl = 0
                with open(tmp, 'wb') as f:
                    for chunk in r.iter_content(8192):
                        dl += len(chunk)
                        f.write(chunk)
                        progress_bar(dl, total, prefix=f"  {Colors.BLUE}DL{Colors.RESET}")
        except Exception as e: log_error(f"Download failed: {e}")

        # Extract
        try:
            if target_dir.exists():
                try: shutil.rmtree(target_dir)
                except: log_error(f"Permission denied removing {target_dir}. Use sudo.")
            
            try:
                target_dir.mkdir(parents=True, exist_ok=True)
                with tarfile.open(tmp, "r:gz") as tar:
                    tar.extractall(target_dir)
            except PermissionError:
                log_error(f"Permission denied writing to {target_dir}. Use sudo.")
            except tarfile.ReadError:
                log_error("Corrupted archive file.")
                
            os.remove(tmp)
            log_success(f"Installed {name} v{version}")
            
        except Exception as e: log_error(f"Extraction failed: {e}")

    def _add_to_swiftlist(self, pkg_name, version):
        # Vérifier si déjà présent
        lines = []
        if os.path.exists(DEP_FILE_NAME):
            with open(DEP_FILE_NAME, 'r') as f:
                lines = f.read().splitlines()
        
        # Mise à jour ou ajout
        found = False
        new_lines = []
        for line in lines:
            if line.strip().startswith(pkg_name):
                new_lines.append(f"{pkg_name} {version}")
                found = True
            else:
                new_lines.append(line)
        
        if not found:
            new_lines.append(f"{pkg_name} {version}")
            
        with open(DEP_FILE_NAME, 'w') as f:
            f.write("\n".join(new_lines) + "\n")
        
        log_info(f"Updated {DEP_FILE_NAME}")

    # --- LINK (DEV LOCAL) ---
    def link_file(self, file_path, alias=None):
        target = Path(file_path)
        if not target.exists(): log_error(f"File {file_path} not found.")
        if not alias: alias = target.stem
        
        log_step("LINK", f"Linking local file {target.name} as '{alias}'...")
        
        # Lecture config
        config = {}
        if Path(CONFIG_FILE_NAME).exists():
            try: 
                with open(CONFIG_FILE_NAME) as f: config = json.load(f)
            except: pass
            
        if "swift" not in config: config["swift"] = {}
        config["swift"][str(target)] = {"as": alias}
        
        with open(CONFIG_FILE_NAME, 'w') as f: json.dump(config, f, indent=4)
        log_success(f"Link saved in {CONFIG_FILE_NAME}")

# ==============================================================================
# MAIN ENTRY POINT
# ==============================================================================
def main():
    parser = argparse.ArgumentParser(prog="zarch", description="Zarch Package Manager v4.0")
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # COMMANDES
    parser_init = subparsers.add_parser("init", help="Initialize project")
    parser_init.add_argument("name", nargs="?", help="Project Name")

    parser_login = subparsers.add_parser("login", help="Login to registry")
    parser_login.add_argument("-name", required=True)
    parser_login.add_argument("-password", required=True)

    subparsers.add_parser("logout", help="Logout")

    subparsers.add_parser("build", help="Build package")

    parser_pub = subparsers.add_parser("publish", help="Publish package")
    parser_pub.add_argument("--linf", dest="file", help="File override")

    parser_inst = subparsers.add_parser("install", help="Install packages")
    parser_inst.add_argument("package", nargs="?", help="Package name")

    parser_link = subparsers.add_parser("link", help="Link local file")
    parser_link.add_argument("file")
    parser_link.add_argument("--as", dest="alias")

    args = parser.parse_args()
    mgr = ZarchManager()

    if args.command == "login": mgr.login(args.name, args.password)
    elif args.command == "logout": mgr.logout()
    elif args.command == "init": mgr.init_project(args.name)
    elif args.command == "build": mgr.build_package()
    elif args.command == "publish": mgr.publish_package(args.file)
    elif args.command == "install": mgr.install(args.package)
    elif args.command == "link": mgr.link_file(args.file, args.alias)
    else: parser.print_help()

if __name__ == "__main__":
    try: main()
    except KeyboardInterrupt: print(f"\n{Colors.YELLOW}[ABORT]{Colors.RESET}")
