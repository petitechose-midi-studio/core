# Desktop Simulator (WebAssembly)

Simulateur desktop de midi-studio avec LVGL + SDL2, compilé en WebAssembly.

## Quick Start

```bash
# Premier lancement (installe Emscripten automatiquement)
./build.sh

# Tester dans le navigateur
./build.sh serve
# Ouvrir http://localhost:8080/midi_studio_wasm.html
```

## Prerequis

- **Git Bash** (Windows) ou terminal Unix
- **PlatformIO CLI** : `pip install platformio`
- **Python 3** : pour le serveur local

Emscripten SDK est installe automatiquement dans `tools/emsdk/`.

## Commandes

| Commande | Description |
|----------|-------------|
| `./build.sh` | Build WASM |
| `./build.sh clean` | Clean + rebuild |
| `./build.sh serve` | Build + serveur local |
| `./build.sh watch` | **Hot reload** - rebuild auto + refresh navigateur |
| `./build.sh setup` | Installe Emscripten sans build |

## Developpement (Hot Reload)

```bash
./build.sh watch
```

Ouvre http://localhost:8080/midi_studio_wasm.html - le navigateur se rafraichit automatiquement quand tu modifies un fichier `.cpp`/`.hpp` dans `src/`.

## Structure

```
desktop/
├── build.sh          # Script de build unique
├── main_wasm.cpp     # Point d'entree Emscripten
├── wasm/
│   ├── CMakeLists.txt    # Config CMake WASM
│   └── shell.html        # Template HTML
├── build/wasm/       # (genere) Fichiers de build
├── bin/wasm/         # (genere) Output WASM
└── tools/emsdk/      # (genere) Emscripten SDK
```

## Output

Apres build, les fichiers sont dans `bin/wasm/`:

| Fichier | Description |
|---------|-------------|
| `midi_studio_wasm.html` | Page HTML a ouvrir |
| `midi_studio_wasm.wasm` | Module WebAssembly (~1.2 MB) |
| `midi_studio_wasm.js` | Glue code JavaScript |

## Troubleshooting

### "emcc not found"
```bash
./build.sh setup
```

### "PlatformIO not found"
```bash
pip install platformio
```

### "lvgl not found"
```bash
cd ..  # midi-studio/core
pio pkg install
```

### Clean complet
```bash
rm -rf build bin tools
./build.sh
```

## Notes techniques

- **SDL2** fourni par Emscripten (`-s USE_SDL=2`)
- **MIDI** desactive (NullMidiTransport)
- **Main loop** via `emscripten_set_main_loop_arg()`
- **Resolution** 480x320 (configurable dans CMakeLists.txt)
