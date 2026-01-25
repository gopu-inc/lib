// zarch.swf - Le Gestionnaire de Paquets Officiel
// À exécuter avec : swift zarch.swf [commande] [args]

import "http";
import "sys";
import "io";
import "json";
import "net"; // Pour vérifier la connexion si besoin

var API_URL = "https://zenv-hub.onrender.com/api";
var REGISTRY_DIR = "./zarch_modules";

// === FONCTIONS UTILITAIRES ===

func print_header() {
    print("========================================");
    print("  ZARCH - Package Manager v1.0");
    print("========================================");
}

func ensure_registry() {
    if (io.exists(REGISTRY_DIR) == false) {
        io.mkdir(REGISTRY_DIR);
    }
}

// === COMMANDES ===

func cmd_install(pkg_name) {
    ensure_registry();
    print("📦  Recherche de ", pkg_name, "...");
    
    // 1. Récupérer les infos du package
    var url = API_URL + "/package/info/global/" + pkg_name;
    var json_resp = http.get(url);
    
    if (json_resp == null) {
        print("❌  Erreur: Impossible de contacter le serveur.");
        return;
    }
    
    // Parser le JSON (via notre helper C)
    var dl_url = json.get(json_resp, "download_url");
    var version = json.get(json_resp, "latest_version");
    
    if (dl_url == null) {
        print("❌  Package introuvable : ", pkg_name);
        return;
    }
    
    print("⬇️   Téléchargement de ", pkg_name, " v", version, "...");
    
    // 2. Télécharger l'archive
    var full_dl_url = "https://zenv-hub.onrender.com" + dl_url;
    var tar_file = REGISTRY_DIR + "/" + pkg_name + ".tar.gz";
    
    var success = http.download(full_dl_url, tar_file);
    
    if (success == "success") {
        print("📦  Extraction...");
        // Utilisation de tar système pour extraire
        var cmd = "tar -xzf " + tar_file + " -C " + REGISTRY_DIR;
        sys.exec(cmd);
        
        // Nettoyage
        sys.exec("rm " + tar_file);
        
        print("✅  Installé avec succès : ", pkg_name);
        
        // Mise à jour de zarch.json local si nécessaire
        update_local_config(pkg_name, version);
        
    } else {
        print("❌  Échec du téléchargement.");
    }
}

func update_local_config(pkg_name, version) {
    if (io.exists("zarch.json")) {
        // TODO: Lire et ajouter la dépendance
        // Pour l'instant on append juste (MVP)
        print("📝  Mise à jour de zarch.json (TODO)");
    }
}

func cmd_link(entry_point, alias) {
    print("🔗  Création du lien symbolique...");
    // Création d'un wrapper .swf qui redirige vers le module
    // Ou utilisation de ln -s via sys.exec
    
    var target = entry_point;
    var link_name = alias;
    
    // Commande système pour créer le lien
    var cmd = "ln -sf " + target + " " + link_name;
    sys.exec(cmd);
    
    print("✅  Lien créé : ", link_name, " -> ", target);
    print("    Vous pouvez maintenant lancer : swift ", link_name);
}

func cmd_help() {
    print("Usage: zarch [commande] [arguments]");
    print("");
    print("Commandes:");
    print("  install <package>   Télécharger et installer un package");
    print("  remove <package>    Supprimer un package");
    print("  search <query>      Rechercher sur le hub");
    print("  link <file> <alias> Créer un raccourci global");
    print("  login               Se connecter au Zenv Hub");
}

// === POINT D'ENTRÉE ===

main() {
    print_header();
    
    // Récupérer le premier argument (la commande)
    var cmd = sys.argv(0);
    
    if (cmd == null) {
        cmd_help();
        return;
    }
    
    if (cmd == "install") {
        var pkg = sys.argv(1);
        if (pkg != null) {
            cmd_install(pkg);
        } else {
            print("❌  Nom du package manquant.");
        }
    } 
    elif (cmd == "link") {
        var entry = sys.argv(1);
        var alias = sys.argv(2);
        if (entry != null) {
            if (alias == null) alias = entry; // Par défaut même nom
            cmd_link(entry, alias);
        } else {
            print("❌  Fichier cible manquant.");
        }
    }
    elif (cmd == "help") {
        cmd_help();
    }
    else {
        print("❌  Commande inconnue : ", cmd);
        cmd_help();
    }
}
