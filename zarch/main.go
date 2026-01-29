package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/fatih/color"
)

// Version du gestionnaire
const Version = "1.0.0"

func main() {
	// Configuration des flags
	initCmd := flag.NewFlagSet("init", flag.ExitOnError)
	installCmd := flag.NewFlagSet("install", flag.ExitOnError)
	publishCmd := flag.NewFlagSet("publish", flag.ExitOnError)
	buildCmd := flag.NewFlagSet("build", flag.ExitOnError)
	loginCmd := flag.NewFlagSet("login", flag.ExitOnError)
	searchCmd := flag.NewFlagSet("search", flag.ExitOnError)
	
	// Flags communs
	helpFlag := flag.Bool("h", false, "Affiche l'aide")
	versionFlag := flag.Bool("v", false, "Affiche la version")
	noDepsFlag := installCmd.Bool("no-get-dep", false, "Ne pas installer les dépendances")
	
	// Parsing des flags globaux
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
		initCmd.Parse(os.Args[2:])
		if initCmd.NArg() < 1 {
			fmt.Println("Usage: zarch init <package-name>")
			os.Exit(1)
		}
		runInit(initCmd.Arg(0))
		
	case "install":
		installCmd.Parse(os.Args[2:])
		args := installCmd.Args()
		if len(args) == 0 {
			fmt.Println("Usage: zarch install [package] ou zarch install (sans args pour installer depuis SwiftList.txt)")
			os.Exit(1)
		}
		runInstall(args[0], *noDepsFlag)
		
	case "publish":
		publishCmd.Parse(os.Args[2:])
		runPublish()
		
	case "build":
		buildCmd.Parse(os.Args[2:])
		runBuild()
		
	case "login":
		loginCmd.Parse(os.Args[2:])
		if loginCmd.NArg() < 2 {
			fmt.Println("Usage: zarch login <username> <password>")
			os.Exit(1)
		}
		runLogin(loginCmd.Arg(0), loginCmd.Arg(1))
		
	case "search":
		searchCmd.Parse(os.Args[2:])
		if searchCmd.NArg() < 1 {
			fmt.Println("Usage: zarch search <query>")
			os.Exit(1)
		}
		runSearch(searchCmd.Arg(0))
		
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
	cyan := color.New(color.FgCyan).SprintFunc()
	green := color.New(color.FgGreen).SprintFunc()
	yellow := color.New(color.FgYellow).SprintFunc()
	
	fmt.Println(cyan("╔══════════════════════════════════════════════════════╗"))
	fmt.Println(cyan("║          ZARCH - Gestionnaire de Packages            ║"))
	fmt.Println(cyan("║                SwiftFlow " + Version + "             ║"))
	fmt.Println(cyan("╚══════════════════════════════════════════════════════╝"))
	fmt.Println()
	fmt.Println(green("[PACK] GESTION DE PACKAGES:"))
	fmt.Println("  " + yellow("init") + " <nom>        Crée un nouveau package")
	fmt.Println("  " + yellow("install") + " [pkg]     Installe un package")
	fmt.Println("  " + yellow("publish") + "           Publie le package courant")
	fmt.Println("  " + yellow("build") + "             Build le package")
	fmt.Println()
	fmt.Println(green("[AUTH] AUTHENTIFICATION:"))
	fmt.Println("  " + yellow("login") + " <user> <pass> Connexion au registre")
	fmt.Println()
	fmt.Println(green("[TOOLS]  OUTILS:"))
	fmt.Println("  " + yellow("search") + " <query>     Recherche de packages")
	fmt.Println("  " + yellow("compile") + " <file>     Compile SwiftFlow en natif")
	fmt.Println()
	fmt.Println(green("[OPT] OPTIONS:"))
	fmt.Println("  " + yellow("-h") + "                Affiche cette aide")
	fmt.Println("  " + yellow("-v") + "                Affiche la version")
	fmt.Println()
	fmt.Println(cyan("[REGISTRY] Registre: https://zenv-hub.onrender.com"))
	fmt.Println(cyan("[DEPOT] Dépôt: https://github.com/gopu-inc/lib"))
}
