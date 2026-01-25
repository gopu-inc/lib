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
from pathlib import Path

# --- Configuration ---
REGISTRY_URL = "https://zenv-hub.onrender.com"
MODULES_DIR = Path("/usr/local/lib/swift")
USER_CONFIG_DIR = Path.home() / ".zarch"
CREDENTIALS_FILE = USER_CONFIG_DIR / "credentials.json"

# Fichiers projet
LOCK_FILE_NAME = "zarch.lock.json"
CONFIG_FILE_NAME = "zarch.json"
DEP_FILE_NAME = "SwiftList.txt"

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

def log_step(step, msg):
    print(f"{Colors.MAGENTA}[{step}]{Colors.RESET} {msg}")

def progress_bar(current, total, prefix="", length=40):
    if total == 0: total = 1
    percent = float(current) * 100 / total
    filled = int(length * current // total)
    bar = '=' * filled + '-' * (length - filled)
    print(f"\r{prefix} [{bar}] {percent:.1f}%", end='', flush=True)
    if current >= total:
        print()

# --- Gestionnaire Zarch ---
class ZarchManager:
    def __init__(self):
        self.session = requests.Session()
        # Création du dossier système si nécessaire
        if not MODULES_DIR.exists():
            try:
                MODULES_DIR.mkdir(parents=True, exist_ok=True)
            except PermissionError:
                pass 

        # Création dossier config utilisateur
        if not USER_CONFIG_DIR.exists():
            USER_CONFIG_DIR.mkdir(parents=True, exist_ok=True)

    # --- AUTHENTIFICATION ---
    def register(self, username, password, email=""):
        log_step("AUTH", f"Creating account for {username}...")
        
        try:
            payload = {"username": username, "password": password, "email": email}
            response = self.session.post(f"{REGISTRY_URL}/api/auth/register", json=payload)
            
            if response.status_code == 200:
                data = response.json()
                token = data.get("token")
                
                # Sauvegarde locale automatique après inscription
                creds = {"username": username, "token": token, "login_time": time.time()}
                with open(CREDENTIALS_FILE, 'w') as f:
                    json.dump(creds, f)
                
                log_success("Account created and logged in successfully.")
            else:
                try:
                    err = response.json().get('error', response.text)
                except:
                    err = response.text
                log_error(f"Registration failed: {err}")
                
        except requests.RequestException as e:
            log_error(f"Connection failed: {e}")

    def login(self, username, password):
        log_step("AUTH", f"Authenticating as {username}...")
        
        try:
            payload = {"username": username, "password": password}
            response = self.session.post(f"{REGISTRY_URL}/api/auth/login", json=payload)
            
            if response.status_code == 200:
                data = response.json()
                token = data.get("token")
                
                # Sauvegarde locale
                creds = {"username": username, "token": token, "login_time": time.time()}
                with open(CREDENTIALS_FILE, 'w') as f:
                    json.dump(creds, f)
                
                log_success("Logged in successfully.")
            else:
                try:
                    err = response.json().get('error', 'Unknown error')
                except:
                    err = response.text
                log_error(f"Login failed: {err}")
                
        except requests.RequestException as e:
            log_error(f"Connection failed: {e}")

    def logout(self):
        if CREDENTIALS_FILE.exists():
            CREDENTIALS_FILE.unlink()
            log_success("Logged out. Credentials removed.")
        else:
            log_warning("No active session found.")

    def _get_token(self):
        if not CREDENTIALS_FILE.exists():
            return None
        try:
            with open(CREDENTIALS_FILE, 'r') as f:
                data = json.load(f)
                return data.get("token")
        except:
            return None

    # --- CMD: INIT ---
    def init_project(self, project_name=None):
        if not project_name:
            project_name = input("Project name: ").strip()
            if not project_name: log_error("Project name is required")

        root_path = Path(project_name)
        if root_path.exists():
            log_error(f"Directory '{project_name}' already exists.")

        log_step("INIT", f"Creating project structure for '{project_name}'...")

        try:
            root_path.mkdir(parents=True)
            (root_path / "src").mkdir()
        except OSError as e:
            log_error(f"Failed to create directories: {e}")

        # zarch.json
        config = {
            "name": project_name,
            "version": "1.0.0",
            "description": "A SwiftFlow package",
            "main": "src/main.swf",
            "author": "",
            "license": "MIT",
            "scope": "user",
            "build": "all"
        }
        with open(root_path / CONFIG_FILE_NAME, 'w') as f:
            json.dump(config, f, indent=4)

        # SwiftList.txt
        with open(root_path / DEP_FILE_NAME, 'w') as f:
            f.write("# SwiftList - Dependencies\n")

        # Fichier source par défaut
        with open(root_path / "src/main.swf", 'w') as f:
            f.write(f"// Package: {project_name}\nprint(\"Package {project_name} loaded\");\n")

        log_success(f"Project '{project_name}' initialized.")
        print(f"  {Colors.WHITE}cd {project_name}{Colors.RESET}")

    # --- CMD: BUILD ---
    def build_package(self):
        if not Path(CONFIG_FILE_NAME).exists():
            log_error(f"{CONFIG_FILE_NAME} not found. Are you in a Zarch project?")

        # 1. Lecture Config
        try:
            with open(CONFIG_FILE_NAME, 'r') as f:
                config = json.load(f)
        except json.JSONDecodeError:
            log_error("Invalid JSON in configuration file.")

        name = config.get("name")
        version = config.get("version")
        build_mode = config.get("build", "none")
        
        if build_mode != "all":
            log_warning(f"Build mode is set to '{build_mode}'. Skipping build.")
            return

        log_step("BUILD", f"Building {name} v{version}...")

        # 2. Préparation DIST
        dist_dir = Path("dist")
        if dist_dir.exists():
            shutil.rmtree(dist_dir)
        dist_dir.mkdir()

        # 3. Génération Lockfile
        lock_data = config.copy()
        lock_data["built_at"] = time.time()
        lock_data["files_included"] = []
        
        # 4. Compression .tar.gz
        tar_filename = f"{name}-v{version}.tar.gz"
        tar_path = dist_dir / tar_filename

        log_info("Compressing package files...")
        
        with tarfile.open(tar_path, "w:gz") as tar:
            for root, dirs, files in os.walk("."):
                if "dist" in root or ".git" in root or "__pycache__" in root:
                    continue
                
                for file in files:
                    full_path = os.path.join(root, file)
                    rel_path = os.path.relpath(full_path, ".")
                    if rel_path == tar_filename: continue
                    
                    tar.add(full_path, arcname=rel_path)
                    lock_data["files_included"].append(rel_path)

        with open(LOCK_FILE_NAME, 'w') as f:
            json.dump(lock_data, f, indent=4)
        
        sz = tar_path.stat().st_size / 1024
        log_success(f"Build complete: dist/{tar_filename} ({sz:.2f} KB)")
        log_info(f"Lockfile generated: {LOCK_FILE_NAME}")

    # --- CMD: PUBLISH ---
    def publish_package(self, file_pattern=None):
        token = self._get_token()
        if not token:
            log_error("You must be logged in to publish. Use 'zarch login' or 'zarch register'.")

        if not Path(CONFIG_FILE_NAME).exists():
            log_error("zarch.json required for publishing.")
        
        with open(CONFIG_FILE_NAME, 'r') as f:
            config = json.load(f)

        name = config.get("name")
        version = config.get("version")
        desc = config.get("description", "")
        scope = config.get("scope", "user")

        if not file_pattern:
            target_file = f"dist/{name}-v{version}.tar.gz"
            if os.path.exists(target_file):
                file_path = target_file
            else:
                files = glob.glob("dist/*.tar.gz")
                if not files:
                    log_error(f"No package found in dist/ for v{version}. Run 'zarch build' first.")
                file_path = files[0]
        else:
            file_path = file_pattern

        log_step("PUBLISH", f"Uploading {file_path} to {REGISTRY_URL}...")
        
        upload_url = f"{REGISTRY_URL}/api/package/upload/{scope}/{name}"
        
        try:
            with open(file_path, 'rb') as f:
                headers = {'Authorization': f'Bearer {token}'}
                files = {'file': (os.path.basename(file_path), f, 'application/gzip')}
                data = {
                    'version': version,
                    'description': desc,
                    'license': config.get('license', 'MIT'),
                    'personal_code': 'CLI_AUTO'
                }
                
                print(f"{Colors.CYAN}[UPLOADING]{Colors.RESET} Sending data...")
                
                response = self.session.post(upload_url, headers=headers, files=files, data=data)
                
                if response.status_code == 200:
                    log_success(f"Published successfully!")
                    print(f"  Package: {name}@{version}")
                    print(f"  URL: {REGISTRY_URL}/package/{scope}/{name}")
                else:
                    try:
                        err = response.json().get('error', response.text)
                    except:
                        err = response.text
                    log_error(f"Upload failed ({response.status_code}): {err}")

        except requests.RequestException as e:
            log_error(f"Connection failed: {e}")

    # --- CMD: INSTALL ---
    def install(self, package_name):
        scope = "user"
        name = package_name
        
        if package_name.startswith("@"):
            parts = package_name[1:].split("/")
            if len(parts) == 2:
                scope, name = parts
        
        log_step("INSTALL", f"Resolving {package_name}...")

        try:
            resp = self.session.get(f"{REGISTRY_URL}/api/package/info/{scope}/{name}")
            if resp.status_code != 200:
                log_error(f"Package not found on registry.")
            
            pkg_info = resp.json()
            version = pkg_info['latest_version']
            dl_url = f"{REGISTRY_URL}{pkg_info['download_url']}"
            
            log_info(f"Found version {version}")
        except Exception as e:
            log_error(f"Network error: {e}")

        tmp_file = f"/tmp/{name}-{version}.tar.gz"
        install_target = MODULES_DIR / name
        
        try:
            with self.session.get(dl_url, stream=True) as r:
                total_len = int(r.headers.get('content-length', 0))
                dl = 0
                with open(tmp_file, 'wb') as f:
                    for chunk in r.iter_content(8192):
                        dl += len(chunk)
                        f.write(chunk)
                        progress_bar(dl, total_len, prefix=f"{Colors.CYAN}[DOWNLOAD]{Colors.RESET}")
            
            log_info(f"Extracting to {install_target}...")
            
            if install_target.exists():
                try:
                    shutil.rmtree(install_target)
                except PermissionError:
                    log_error(f"Permission denied. Run with sudo.")

            try:
                install_target.mkdir(parents=True, exist_ok=True)
                with tarfile.open(tmp_file, "r:gz") as tar:
                    tar.extractall(install_target)
            except PermissionError:
                log_error(f"Permission denied writing to {install_target}. Run with sudo.")

            os.remove(tmp_file)
            log_success(f"Package {package_name} installed.")

        except Exception as e:
            log_error(f"Install failed: {e}")

    # --- CMD: LINK ---
    def link_file(self, file_path, alias=None):
        target = Path(file_path)
        if not target.exists():
            log_error(f"File {file_path} not found.")
        
        if not alias:
            alias = target.stem

        log_step("LINK", f"Linking {file_path} as '{alias}'...")

        config = {}
        if Path(CONFIG_FILE_NAME).exists():
            with open(CONFIG_FILE_NAME, 'r') as f:
                try: config = json.load(f)
                except: pass
        
        if "swift" not in config: config["swift"] = {}
        config["swift"][str(target)] = {"as": alias}

        with open(CONFIG_FILE_NAME, 'w') as f:
            json.dump(config, f, indent=4)
            
        log_success(f"Link added to {CONFIG_FILE_NAME}")

# --- CLI Entry Point ---
def main():
    parser = argparse.ArgumentParser(prog="zarch", description="Zarch Package Manager v3.1")
    subparsers = parser.add_subparsers(dest="command", help="Command")

    # REGISTER (NOUVEAU)
    parser_reg = subparsers.add_parser("register", help="Create an account")
    parser_reg.add_argument("-name", dest="username", required=True, help="Username")
    parser_reg.add_argument("-password", dest="password", required=True, help="Password")
    parser_reg.add_argument("-email", dest="email", default="", help="Optional Email")

    # LOGIN
    parser_login = subparsers.add_parser("login", help="Authenticate with registry")
    parser_login.add_argument("-name", dest="username", required=True, help="Username")
    parser_login.add_argument("-password", dest="password", required=True, help="Password")

    # LOGOUT
    parser_logout = subparsers.add_parser("logout", help="Clear credentials")

    # INIT
    parser_init = subparsers.add_parser("init", help="Initialize new package")
    parser_init.add_argument("name", nargs="?", help="Project name")

    # BUILD
    parser_build = subparsers.add_parser("build", help="Build package for release")

    # PUBLISH
    parser_pub = subparsers.add_parser("publish", help="Publish package to registry")
    parser_pub.add_argument("--linf", dest="file", help="Specific file pattern to upload")

    # INSTALL
    parser_inst = subparsers.add_parser("install", help="Install dependency")
    parser_inst.add_argument("package", help="Package name")

    # LINK
    parser_link = subparsers.add_parser("link", help="Link local file")
    parser_link.add_argument("file", help="Path to .swf file")
    parser_link.add_argument("--as", dest="alias", help="Import alias")

    args = parser.parse_args()
    manager = ZarchManager()

    if args.command == "register":
        manager.register(args.username, args.password, args.email)
    elif args.command == "login":
        manager.login(args.username, args.password)
    elif args.command == "logout":
        manager.logout()
    elif args.command == "init":
        manager.init_project(args.name)
    elif args.command == "build":
        manager.build_package()
    elif args.command == "publish":
        manager.publish_package(args.file)
    elif args.command == "install":
        manager.install(args.package)
    elif args.command == "link":
        manager.link_file(args.file, args.alias)
    else:
        parser.print_help()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}[ABORT]{Colors.RESET} Stopped by user.")
