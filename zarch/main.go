package main

import (
	"flag"
	"fmt"
	"os"
)

const Version = "1.0.0"

func main() {
	// Flags globaux
	helpFlag := flag.Bool("h", false, "Affiche l'aide")
	versionFlag := flag.Bool("v", false, "Affiche la version")
	flag.Parse()

	if *helpFlag {
		printHelp()
		return
	}

	if *versionFlag {
		fmt.Printf("zarch (gestionnaire de packages SwiftFlow) version %s\n", Version)
		return
	}

	if len(os.Args) < 2 {
		printHelp()
		os.Exit(1)
	}

	// Routing des commandes
	switch os.Args[1] {
	case "init":
		if len(os.Args) < 3 {
			fmt.Println("Usage: zarch init <package-name>")
			os.Exit(1)
		}
		runInit(os.Args[2])

	case "install":
		var packageName string
		noDeps := false
		
		if len(os.Args) >= 3 {
			if os.Args[2] == "--no-get-dep" {
				noDeps = true
				if len(os.Args) >= 4 {
					packageName = os.Args[3]
				}
			} else {
				packageName = os.Args[2]
			}
		}
		runInstall(packageName, noDeps)

	case "publish":
		runPublish()

	case "build":
		runBuild()

	case "login":
		if len(os.Args) < 4 {
			fmt.Println("Usage: zarch login <username> <password>")
			os.Exit(1)
		}
		runLogin(os.Args[2], os.Args[3])

	case "search":
		if len(os.Args) < 3 {
			fmt.Println("Usage: zarch search <query>")
			os.Exit(1)
		}
		runSearch(os.Args[2])

	case "compile":
		if len(os.Args) < 3 {
			fmt.Println("Usage: zarch compile <file.swf>")
			os.Exit(1)
		}
		runCompile(os.Args[2])

	default:
		fmt.Printf("Commande inconnue: %s\n", os.Args[1])
		printHelp()
		os.Exit(1)
	}
}

func printHelp() {
	fmt.Println("╔══════════════════════════════════════════════════════╗")
	fmt.Println("║          ZARCH - Gestionnaire de Packages           ║")
	fmt.Println("║                SwiftFlow v1.0.0                     ║")
	fmt.Println("╚══════════════════════════════════════════════════════╝")
	fmt.Println()
	fmt.Println("📦 GESTION DE PACKAGES:")
	fmt.Println("  init <nom>        Crée un nouveau package")
	fmt.Println("  install [pkg]     Installe un package")
	fmt.Println("  publish           Publie le package courant")
	fmt.Println("  build             Build le package")
	fmt.Println()
	fmt.Println("🔐 AUTHENTIFICATION:")
	fmt.Println("  login <user> <pass> Connexion au registre")
	fmt.Println()
	fmt.Println("🛠️  OUTILS:")
	fmt.Println("  search <query>     Recherche de packages")
	fmt.Println("  compile <file>     Compile SwiftFlow en natif")
	fmt.Println()
	fmt.Println("📋 OPTIONS:")
	fmt.Println("  -h                Affiche cette aide")
	fmt.Println("  -v                Affiche la version")
	fmt.Println("  --no-get-dep      Ne pas installer les dépendances")
	fmt.Println()
	fmt.Println("🌐 Registre: https://zenv-hub.onrender.com")
	fmt.Println("📚 Dépôt: https://github.com/gopu-inc/lib")
}
