package main

import (
	"archive/tar"
	"mime/multipart"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/fatih/color"
	"gopkg.in/yaml.v3"
)

// ======================================================
// STRUCTURES DE DONNÉES
// ======================================================

type ZarchConfig struct {
	Name        string            `json:"name"`
	Version     string            `json:"version"`
	Description string            `json:"description"`
	Author      string            `json:"author"`
	License     string            `json:"license"`
	EntryPoint  string            `json:"entry_point"`
	Dependencies map[string]string `json:"dependencies"`
	Scripts     map[string]string `json:"scripts"`
	Markdown    []string          `json:"markdown"`
}

type PackageInfo struct {
	Name        string `json:"name"`
	Scope       string `json:"scope"`
	Version     string `json:"version"`
	Description string `json:"description"`
	Author      string `json:"author"`
	DownloadURL string `json:"download_url"`
	Size        int    `json:"size"`
	SHA256      string `json:"sha256"`
}

type UserCredentials struct {
	Username string `json:"username"`
	Token    string `json:"token"`
	Expiry   string `json:"expiry"`
}

// ======================================================
// COMMANDES PRINCIPALES
// ======================================================

func runInit(packageName string) {
	green := color.New(color.FgGreen).SprintFunc()
	cyan := color.New(color.FgCyan).SprintFunc()
	
	fmt.Println(green("🚀 Initialisation du package: ") + cyan(packageName))
	
	// Création du dossier
	if err := os.MkdirAll(packageName, 0755); err != nil {
		fmt.Printf("❌ Erreur création dossier: %v\n", err)
		return
	}
	
	// Création de zarch.json
	config := ZarchConfig{
		Name:        packageName,
		Version:     "1.0.0",
		Description: "Un package SwiftFlow",
		Author:      "anonymous",
		License:     "MIT",
		EntryPoint:  "main.swf",
		Dependencies: make(map[string]string),
		Scripts: map[string]string{
			"test": "swift test.swf",
		},
		Markdown: []string{
			"# " + packageName,
			"",
			"## Description",
			"Un package SwiftFlow généré avec zarch.",
			"",
			"## Installation",
			"```bash",
			"zarch install " + packageName,
			"```",
		},
	}
	
	configBytes, _ := json.MarshalIndent(config, "", "  ")
	configPath := filepath.Join(packageName, "zarch.json")
	if err := os.WriteFile(configPath, configBytes, 0644); err != nil {
		fmt.Printf("❌ Erreur création zarch.json: %v\n", err)
		return
	}
	
	// Création de SwiftList.txt (dépendances)
	depsPath := filepath.Join(packageName, "SwiftList.txt")
	depsContent := "# Dépendances du package\n# Format: package@version\n\n"
	if err := os.WriteFile(depsPath, []byte(depsContent), 0644); err != nil {
		fmt.Printf("❌ Erreur création SwiftList.txt: %v\n", err)
		return
	}
	
	// Création du fichier d'entrée
	entryPath := filepath.Join(packageName, "main.swf")
	entryContent := "// Package: " + packageName + "\n// Version: 1.0.0\n\nexport {\n    // Exporter vos fonctions ici\n};\n\nfunc main() {\n    print(\"Hello from \" + \"" + packageName + "\");\n}\n\nmain();\n"
	if err := os.WriteFile(entryPath, []byte(entryContent), 0644); err != nil {
		fmt.Printf("❌ Erreur création main.swf: %v\n", err)
		return
	}
	
	// Création de README.md
	readmePath := filepath.Join(packageName, "README.md")
	readmeContent := strings.Join(config.Markdown, "\n")
	if err := os.WriteFile(readmePath, []byte(readmeContent), 0644); err != nil {
		fmt.Printf("❌ Erreur création README.md: %v\n", err)
		return
	}
	
	// Création de .gitignore
	gitignorePath := filepath.Join(packageName, ".gitignore")
	gitignoreContent := "# Dependencies\nnode_modules/\n\n# Build artifacts\ndist/\nbuild/\n\n# Environment\n.env\n\n# IDE\n.vscode/\n.idea/\n\n# OS\n.DS_Store\n"
	if err := os.WriteFile(gitignorePath, []byte(gitignoreContent), 0644); err != nil {
		fmt.Printf("❌ Erreur création .gitignore: %v\n", err)
		return
	}
	
	fmt.Println(green("✅ Package créé avec succès!"))
	fmt.Println(cyan("📁 Structure:"))
	fmt.Println("  📄 zarch.json      - Configuration du package")
	fmt.Println("  📄 SwiftList.txt   - Liste des dépendances")
	fmt.Println("  📄 main.swf        - Point d'entrée")
	fmt.Println("  📄 README.md       - Documentation")
	fmt.Println("  📄 .gitignore      - Fichiers ignorés")
	fmt.Println()
	fmt.Println(green("🎯 Prochaines étapes:"))
	fmt.Println("  1. cd " + packageName)
	fmt.Println("  2. Modifiez zarch.json avec vos informations")
	fmt.Println("  3. Ajoutez vos dépendances dans SwiftList.txt")
	fmt.Println("  4. zarch build    - Pour compiler")
	fmt.Println("  5. zarch publish  - Pour publier")
}

func runInstall(packageName string, noDeps bool) {
	green := color.New(color.FgGreen).SprintFunc()
	yellow := color.New(color.FgYellow).SprintFunc()
	
	if packageName == "" {
		// Installation depuis SwiftList.txt
		fmt.Println(green("📦 Installation des dépendances depuis SwiftList.txt..."))
		installFromSwiftList()
		return
	}
	
	fmt.Println(green("📥 Installation de: ") + yellow(packageName))
	
	// Vérifier si c'est une installation locale ou distante
	if strings.HasPrefix(packageName, "./") || strings.HasPrefix(packageName, "../") || strings.HasPrefix(packageName, "/") {
		installLocalPackage(packageName)
		return
	}
	
	// Installation depuis le registre
	installFromRegistry(packageName, noDeps)
}

func runPublish() {
	green := color.New(color.FgGreen).SprintFunc()
	red := color.New(color.FgRed).SprintFunc()
	
	// 1. Vérifier si on est connecté
	token, err := getAuthToken()
	if err != nil {
		fmt.Println(red("❌ Non authentifié. Utilisez 'zarch login' d'abord."))
		return
	}
	
	// 2. Lire la configuration
	config, err := readZarchConfig()
	if err != nil {
		fmt.Println(red("❌ zarch.json non trouvé. Exécutez depuis un dossier de package."))
		return
	}
	
	fmt.Println(green("🚀 Publication du package: ") + config.Name + " v" + config.Version)
	
	// 3. Build du package
	fmt.Println(green("🔨 Build en cours..."))
	buildPath, err := buildPackage()
	if err != nil {
		fmt.Println(red("❌ Erreur lors du build: ") + err.Error())
		return
	}
	
	// 4. Création de l'archive tar.gz
	fmt.Println(green("📦 Création de l'archive..."))
	archivePath, err := createPackageArchive(config, buildPath)
	if err != nil {
		fmt.Println(red("❌ Erreur création archive: ") + err.Error())
		return
	}
	
	// 5. Upload vers le registre
	fmt.Println(green("☁️  Upload vers le registre..."))
	err = uploadToRegistry(config, archivePath, token)
	if err != nil {
		fmt.Println(red("❌ Erreur upload: ") + err.Error())
		return
	}
	
	// 6. Nettoyage
	os.Remove(archivePath)
	
	fmt.Println(green("✅ Package publié avec succès!"))
	fmt.Println(green("🌐 Disponible sur: https://zenv-hub.onrender.com/package/" + config.Name))
}

func runBuild() {
	green := color.New(color.FgGreen).SprintFunc()
	yellow := color.New(color.FgYellow).SprintFunc()
	
	// 1. Lire la configuration
	config, err := readZarchConfig()
	if err != nil {
		fmt.Println("❌ zarch.json non trouvé. Exécutez depuis un dossier de package.")
		return
	}
	
	fmt.Println(green("🔨 Build du package: ") + config.Name + " v" + config.Version)
	
	// 2. Créer le dossier dist/
	distDir := "dist"
	if err := os.MkdirAll(distDir, 0755); err != nil {
		fmt.Println("❌ Erreur création dossier dist/: " + err.Error())
		return
	}
	
	// 3. Compiler si nécessaire
	if hasSwiftFiles() {
		fmt.Println(yellow("⚡ Compilation des fichiers SwiftFlow..."))
		if err := compileSwiftFiles(); err != nil {
			fmt.Println("❌ Erreur compilation: " + err.Error())
			return
		}
	}
	
	// 4. Copier les fichiers nécessaires
	fmt.Println(yellow("📋 Copie des fichiers..."))
	filesToCopy := []string{
		"zarch.json",
		"README.md",
		config.EntryPoint,
	}
	
	for _, file := range filesToCopy {
		if _, err := os.Stat(file); err == nil {
			src := file
			dst := filepath.Join(distDir, file)
			
			// Créer les sous-dossiers si nécessaire
			dstDir := filepath.Dir(dst)
			os.MkdirAll(dstDir, 0755)
			
			srcContent, _ := os.ReadFile(src)
			os.WriteFile(dst, srcContent, 0644)
		}
	}
	
	// 5. Générer le fichier d'installation
	installScript := generateInstallScript(config)
	installPath := filepath.Join(distDir, "install.sh")
	os.WriteFile(installPath, []byte(installScript), 0755)
	
	fmt.Println(green("✅ Build terminé! Dossier: ") + distDir)
}

func runLogin(username, password string) {
	green := color.New(color.FgGreen).SprintFunc()
	red := color.New(color.FgRed).SprintFunc()
	
	fmt.Println(green("🔐 Connexion au registre Zarch..."))
	
	// Préparer la requête
	loginData := map[string]string{
		"username": username,
		"password": password,
	}
	
	jsonData, _ := json.Marshal(loginData)
	
	// Envoyer la requête
	resp, err := http.Post("https://zenv-hub.onrender.com/api/auth/login", 
		"application/json", 
		strings.NewReader(string(jsonData)))
	
	if err != nil {
		fmt.Println(red("❌ Erreur de connexion: ") + err.Error())
		return
	}
	defer resp.Body.Close()
	
	if resp.StatusCode != 200 {
		body, _ := io.ReadAll(resp.Body)
		fmt.Println(red("❌ Échec authentification: ") + string(body))
		return
	}
	
	var result map[string]interface{}
	json.NewDecoder(resp.Body).Decode(&result)
	
	if token, ok := result["token"].(string); ok {
		// Sauvegarder les credentials
		creds := UserCredentials{
			Username: username,
			Token:    token,
			Expiry:   time.Now().Add(24 * time.Hour).Format(time.RFC3339),
		}
		
		credsBytes, _ := json.Marshal(creds)
		home, _ := os.UserHomeDir()
		zarchDir := filepath.Join(home, ".zarch")
		os.MkdirAll(zarchDir, 0755)
		
		credsPath := filepath.Join(zarchDir, "credentials.json")
		os.WriteFile(credsPath, credsBytes, 0600)
		
		fmt.Println(green("✅ Connexion réussie!"))
		fmt.Println(green("🔑 Token sauvegardé dans: ") + credsPath)
	} else {
		fmt.Println(red("❌ Token non reçu"))
	}
}

func runSearch(query string) {
	cyan := color.New(color.FgCyan).SprintFunc()
	green := color.New(color.FgGreen).SprintFunc()
	
	fmt.Println(cyan("🔍 Recherche: ") + query)
	
	resp, err := http.Get(fmt.Sprintf("https://zenv-hub.onrender.com/api/package/search?q=%s", query))
	if err != nil {
		fmt.Println("❌ Erreur recherche: " + err.Error())
		return
	}
	defer resp.Body.Close()
	
	var result map[string]interface{}
	json.NewDecoder(resp.Body).Decode(&result)
	
	if results, ok := result["results"].([]interface{}); ok && len(results) > 0 {
		fmt.Println(green("📦 Packages trouvés:"))
		for i, pkg := range results {
			if pkgMap, ok := pkg.(map[string]interface{}); ok {
				name := pkgMap["name"].(string)
				scope := pkgMap["scope"].(string)
				version := pkgMap["version"].(string)
				desc := pkgMap["description"].(string)
				
				fmt.Printf("%d. @%s/%s v%s\n", i+1, scope, name, version)
				fmt.Printf("   %s\n", desc)
				fmt.Println()
			}
		}
	} else {
		fmt.Println("📭 Aucun package trouvé")
	}
}

func runCompile(filePath string) {
	green := color.New(color.FgGreen).SprintFunc()
	yellow := color.New(color.FgYellow).SprintFunc()
	
	fmt.Println(green("⚡ Compilation SwiftFlow -> Code Machine"))
	fmt.Println(yellow("Fichier: ") + filePath)
	
	// Vérifier l'existence du fichier
	if _, err := os.Stat(filePath); os.IsNotExist(err) {
		fmt.Println("❌ Fichier non trouvé: " + filePath)
		return
	}
	
	// Pour le MVP, on va simplement copier le fichier et l'exécuter avec swift
	// Dans une version future, on implémentera un vrai compilateur
	
	// Créer un wrapper en C
	cCode := generateCWrapper(filePath)
	cFile := strings.TrimSuffix(filePath, ".swf") + ".c"
	
	if err := os.WriteFile(cFile, []byte(cCode), 0644); err != nil {
		fmt.Println("❌ Erreur création wrapper C: " + err.Error())
		return
	}
	
	// Compiler avec gcc
	outputFile := strings.TrimSuffix(filePath, ".swf")
	fmt.Println(yellow("🔨 Compilation avec gcc..."))
	
	cmd := exec.Command("gcc", "-o", outputFile, cFile, "-O2")
	if err := cmd.Run(); err != nil {
		fmt.Println("❌ Erreur compilation C: " + err.Error())
		return
	}
	
	// Nettoyer
	os.Remove(cFile)
	
	fmt.Println(green("✅ Compilation réussie!"))
	fmt.Println(green("🎯 Exécutable: ") + outputFile)
	fmt.Println(yellow("⚠️  Note: Ceci est un MVP. La vraie compilation SwiftFlow est en développement."))
}

// ======================================================
// FONCTIONS AUXILIAIRES
// ======================================================

func readZarchConfig() (*ZarchConfig, error) {
	data, err := os.ReadFile("zarch.json")
	if err != nil {
		return nil, err
	}
	
	var config ZarchConfig
	if err := json.Unmarshal(data, &config); err != nil {
		return nil, err
	}
	
	return &config, nil
}

func getAuthToken() (string, error) {
	home, _ := os.UserHomeDir()
	credsPath := filepath.Join(home, ".zarch", "credentials.json")
	
	data, err := os.ReadFile(credsPath)
	if err != nil {
		return "", err
	}
	
	var creds UserCredentials
	if err := json.Unmarshal(data, &creds); err != nil {
		return "", err
	}
	
	// Vérifier l'expiration
	expiry, _ := time.Parse(time.RFC3339, creds.Expiry)
	if time.Now().After(expiry) {
		return "", fmt.Errorf("token expiré")
	}
	
	return creds.Token, nil
}

func buildPackage() (string, error) {
	// Simuler un build pour le MVP
	// Dans la vraie version, on compilerait les fichiers SwiftFlow
	return ".", nil
}

func createPackageArchive(config *ZarchConfig, sourcePath string) (string, error) {
	// Nom de l'archive
	archiveName := fmt.Sprintf("%s-v%s.tar.gz", config.Name, config.Version)
	
	// Créer l'archive
	file, err := os.Create(archiveName)
	if err != nil {
		return "", err
	}
	defer file.Close()
	
	// Créer le writer gzip
	gzWriter := gzip.NewWriter(file)
	defer gzWriter.Close()
	
	// Créer le writer tar
	tarWriter := tar.NewWriter(gzWriter)
	defer tarWriter.Close()
	
	// Fonction pour ajouter un fichier à l'archive
	addFileToTar := func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		
		// Ignorer certains dossiers/fichiers
		if info.IsDir() && (info.Name() == ".git" || info.Name() == "node_modules") {
			return filepath.SkipDir
		}
		
		// Préparer l'en-tête tar
		header, err := tar.FileInfoHeader(info, "")
		if err != nil {
			return err
		}
		
		// Ajuster le chemin dans l'archive
		relPath, _ := filepath.Rel(sourcePath, path)
		header.Name = filepath.Join(config.Name, relPath)
		
		// Écrire l'en-tête
		if err := tarWriter.WriteHeader(header); err != nil {
			return err
		}
		
		// Si c'est un fichier, écrire son contenu
		if !info.IsDir() {
			file, err := os.Open(path)
			if err != nil {
				return err
			}
			defer file.Close()
			
			_, err = io.Copy(tarWriter, file)
			return err
		}
		
		return nil
	}
	
	// Parcourir les fichiers
	if err := filepath.Walk(sourcePath, addFileToTar); err != nil {
		return "", err
	}
	
	return archiveName, nil
}

func uploadToRegistry(config *ZarchConfig, archivePath string, token string) error {
	// Ouvrir le fichier
	file, err := os.Open(archivePath)
	if err != nil {
		return err
	}
	defer file.Close()
	
	// Lire le contenu pour calculer le hash
	fileData, _ := io.ReadAll(io.MultiReader(file))
	file.Seek(0, 0) // Reset pour le multipart
	
	// Calculer SHA256
	hash := sha256.Sum256(fileData)
	sha256Str := hex.EncodeToString(hash[:])
	
	// Préparer la requête multipart
	body := &strings.Builder{}
	writer := multipart.NewWriter(body)
	
	// Ajouter les champs
	writer.WriteField("version", config.Version)
	writer.WriteField("description", config.Description)
	
	// Ajouter le fichier
	part, _ := writer.CreateFormFile("file", filepath.Base(archivePath))
	io.Copy(part, file)
	
	writer.Close()
	
	// Créer la requête
	req, _ := http.NewRequest("POST", 
		fmt.Sprintf("https://zenv-hub.onrender.com/api/package/upload/user/%s", config.Name),
		strings.NewReader(body.String()))
	
	req.Header.Set("Content-Type", writer.FormDataContentType())
	req.Header.Set("Authorization", "Bearer "+token)
	
	// Envoyer
	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	
	if resp.StatusCode != 200 {
		body, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("échec upload: %s", string(body))
	}
	
	return nil
}

func installFromSwiftList() {
	data, err := os.ReadFile("SwiftList.txt")
	if err != nil {
		fmt.Println("❌ SwiftList.txt non trouvé")
		return
	}
	
	lines := strings.Split(string(data), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		
		// Format: package@version
		parts := strings.Split(line, "@")
		if len(parts) == 2 {
			installFromRegistry(parts[0], false)
		} else {
			installFromRegistry(line, false)
		}
	}
}

func installFromRegistry(packageName string, noDeps bool) {
	// Pour le MVP, on va simplement télécharger et extraire
	fmt.Printf("📥 Téléchargement de %s depuis le registre...\n", packageName)
	
	// URL de téléchargement
	url := fmt.Sprintf("https://zenv-hub.onrender.com/package/download/user/%s/latest", packageName)
	
	resp, err := http.Get(url)
	if err != nil {
		fmt.Printf("❌ Erreur téléchargement: %v\n", err)
		return
	}
	defer resp.Body.Close()
	
	// Créer le fichier temporaire
	tmpFile, err := os.CreateTemp("", "zarch-*.tar.gz")
	if err != nil {
		fmt.Printf("❌ Erreur création fichier: %v\n", err)
		return
	}
	defer os.Remove(tmpFile.Name())
	
	// Copier le contenu
	_, err = io.Copy(tmpFile, resp.Body)
	if err != nil {
		fmt.Printf("❌ Erreur copie: %v\n", err)
		return
	}
	tmpFile.Close()
	
	// Extraire
	fmt.Println("📂 Extraction...")
	if err := extractTarGz(tmpFile.Name(), "."); err != nil {
		fmt.Printf("❌ Erreur extraction: %v\n", err)
		return
	}
	
	fmt.Println("✅ Installation terminée!")
}

func installLocalPackage(path string) {
	fmt.Printf("📦 Installation locale depuis: %s\n", path)
	
	// Copier les fichiers
	err := filepath.Walk(path, func(srcPath string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		
		// Calculer le chemin de destination
		relPath, _ := filepath.Rel(path, srcPath)
		dstPath := filepath.Join(".", relPath)
		
		if info.IsDir() {
			return os.MkdirAll(dstPath, 0755)
		}
		
		// Copier le fichier
		data, err := os.ReadFile(srcPath)
		if err != nil {
			return err
		}
		
		return os.WriteFile(dstPath, data, info.Mode())
	})
	
	if err != nil {
		fmt.Printf("❌ Erreur copie: %v\n", err)
		return
	}
	
	fmt.Println("✅ Installation locale terminée!")
}

func hasSwiftFiles() bool {
	files, _ := filepath.Glob("*.swf")
	return len(files) > 0
}

func compileSwiftFiles() error {
	// Pour le MVP, on va simplement copier les fichiers
	// Dans la vraie version, on compilerait vers du code machine
	
	files, _ := filepath.Glob("*.swf")
	for _, file := range files {
		fmt.Printf("  📄 %s\n", file)
	}
	
	return nil
}

func generateInstallScript(config *ZarchConfig) string {
	return `#!/bin/bash
# Script d'installation pour ` + config.Name + `

echo "📦 Installation de ` + config.Name + ` v` + config.Version + `..."

# Créer les dossiers nécessaires
mkdir -p /usr/local/lib/swift
mkdir -p /usr/local/bin

# Copier les fichiers
cp -r . /usr/local/lib/swift/` + config.Name + `

# Créer le lien symbolique
if [ -f "` + config.EntryPoint + `" ]; then
    ln -sf /usr/local/lib/swift/` + config.Name + `/` + config.EntryPoint + ` /usr/local/bin/` + config.Name + `
    chmod +x /usr/local/bin/` + config.Name + `
    echo "✅ ` + config.Name + ` installé dans /usr/local/bin/"
else
    echo "⚠️  Fichier d'entrée non trouvé: ` + config.EntryPoint + `"
fi

echo "🎉 Installation terminée!"
`
}

func generateCWrapper(swiftFile string) string {
	// Générer un wrapper C simple qui exécute swift
	return `#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    char command[1024];
    
    // Construire la commande
    snprintf(command, sizeof(command), "swift %s", "` + swiftFile + `");
    
    // Exécuter
    return system(command);
}
`
}

func extractTarGz(src, dst string) error {
	file, err := os.Open(src)
	if err != nil {
		return err
	}
	defer file.Close()
	
	gzr, err := gzip.NewReader(file)
	if err != nil {
		return err
	}
	defer gzr.Close()
	
	tr := tar.NewReader(gzr)
	
	for {
		header, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
		
		target := filepath.Join(dst, header.Name)
		
		switch header.Typeflag {
		case tar.TypeDir:
			if err := os.MkdirAll(target, 0755); err != nil {
				return err
			}
			
		case tar.TypeReg:
			// Créer les dossiers parents
			if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
				return err
			}
			
			// Créer le fichier
			file, err := os.OpenFile(target, os.O_CREATE|os.O_RDWR, os.FileMode(header.Mode))
			if err != nil {
				return err
			}
			
			// Copier le contenu
			if _, err := io.Copy(file, tr); err != nil {
				file.Close()
				return err
			}
			file.Close()
		}
	}
	
	return nil
}
