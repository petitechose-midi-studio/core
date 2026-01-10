# Desktop Simulator

Simulateur desktop de midi-studio avec LVGL + SDL2.

## Quick Start

```bash
# Premier lancement (télécharge les outils automatiquement)
./build.sh

# Lancer l'app
./bin/midi_studio_desktop.exe
```

## Prérequis

### Obligatoires
- **Git Bash** (Windows) ou terminal Unix
- **PlatformIO CLI** : `pip install platformio`

### Installés automatiquement
Le script `build.sh` utilise les outils de `open-control/ui-lvgl-components/tools/` :
- **Zig 0.15.2** - Compilateur C/C++ (rapide, cache intelligent)
- **Ninja 1.13.2** - Build system parallèle
- **CMake 4.2.1** - Configuration du build

Si les outils ne sont pas présents, lancer :
```bash
cd ../../../open-control/ui-lvgl-components
./init_env.sh
```

## Structure

```
desktop/
├── build.sh          # Script de build principal
├── run.sh            # Lancer l'app (avec auto-rebuild)
├── CMakeLists.txt    # Configuration CMake
├── main.cpp          # Point d'entrée desktop
├── .vscode/          # Config VSCode (debug, IntelliSense)
├── build/            # (généré) Fichiers de build
├── bin/              # (généré) Exécutable
└── deps/             # (généré) SDL2, SDL2_gfx
```

## Commandes

| Commande | Description |
|----------|-------------|
| `./build.sh` | Build Debug (par défaut) |
| `./build.sh Release` | Build optimisé |
| `./run.sh` | Build + lance l'app |
| `./watch.sh` | **Watch mode** - rebuild auto sur modif |
| `rm -rf build deps` | Clean complet |

## Watch Mode (Hot Reload)

Le script `watch.sh` surveille les fichiers sources et rebuild+relance automatiquement :

```bash
./watch.sh          # Watch en mode Debug
./watch.sh Release  # Watch en mode Release
```

**Dossiers surveillés :**
- `src/` - Code applicatif
- `config/` - Configuration
- `desktop/` - Code spécifique desktop
- `open-control/` - Framework

**Performance :**
- Avec `watchexec` : détection instantanée (~300ms debounce)
- Sans : polling toutes les 2s

**Installer watchexec (recommandé) :**
```bash
# Windows
winget install watchexec

# Linux/macOS
cargo install watchexec-cli
```

## VSCode

Ouvrir le dossier `desktop/` dans VSCode pour :
- **IntelliSense** via `compile_commands.json`
- **Debug GDB** avec breakpoints
- **Build** : Ctrl+Shift+B

### Configurations disponibles
- `Build (Zig+Ninja)` - Build rapide (défaut)
- `Desktop Debug (GDB)` - Debug avec breakpoints
- `Desktop Debug (no build)` - Debug sans rebuild

## Performance

| Type de build | Temps |
|--------------|-------|
| Premier build (from scratch) | ~2-3 min |
| Build incrémental (1 fichier modifié) | ~3 sec |
| Build incrémental (no-op) | <1 sec |

### Pourquoi c'est rapide ?
- **Zig** : Cache de compilation intelligent, pas de preprocessing lourd
- **Ninja** : Build system minimal, parallélisation optimale
- **CMake** : Configuration une seule fois, rebuilds incrémentaux

## Dépendances téléchargées

Au premier build, CMake télécharge automatiquement :
- **SDL2 2.30.10** - Fenêtrage, input, rendu
- **SDL2_gfx** - Primitives graphiques (cercles, arcs)

Ces dépendances sont stockées dans `deps/` et ne sont pas re-téléchargées.

## Troubleshooting

### "Tools not initialized"
```bash
cd ../../../open-control/ui-lvgl-components
./init_env.sh
```

### "PlatformIO not found"
```bash
pip install platformio
```

### Build lent ?
Vérifier que Ninja est bien utilisé :
```bash
head -1 build/CMakeCache.txt | grep Ninja
```

### Erreurs IntelliSense ?
1. Rebuild pour générer `compile_commands.json`
2. Recharger VSCode (Ctrl+Shift+P → "Reload Window")

### Clean complet
```bash
rm -rf build deps bin
./build.sh
```
