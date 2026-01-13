# SERENA - Refactoring Transport Architecture

> **Objectif** : Renommer `ISerialTransport` → `IFrameTransport` et `serial()` → `frames()` pour une sémantique claire et sans ambiguïté.
>
> **Principe** : Le transport (Serial, UDP, TCP) et le framing (COBS, Raw) sont des détails d'implémentation. L'interface expose uniquement l'envoi/réception de frames.

---

## CONTEXTE

### Problème Actuel
- `ISerialTransport` implique "Serial" mais c'est agnostique du transport
- Le contrat mentionne COBS mais UDP n'en a pas besoin
- Confusion sémantique entre transport physique et abstraction

### Solution
- `IFrameTransport` : interface pour envoi/réception de frames binaires
- `frames()` : accesseur dans Context (vs `midi()` pour MIDI)
- Implémentations explicites : `CobsSerialTransport`, `UdpFrameTransport`

---

## INVENTAIRE EXHAUSTIF DES CHANGEMENTS

### 1. FICHIER À RENOMMER

| Repo | Ancien Path | Nouveau Path |
|------|-------------|--------------|
| open-control/framework | `src/oc/hal/ISerialTransport.hpp` | `src/oc/hal/IFrameTransport.hpp` |

### 2. CHANGEMENTS DANS open-control/framework

#### 2.1 src/oc/hal/IFrameTransport.hpp (ex-ISerialTransport.hpp)

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 24 | `class ISerialTransport {` | `class IFrameTransport {` |
| 26 | `virtual ~ISerialTransport() = default;` | `virtual ~IFrameTransport() = default;` |
| 14 | (docstring) "Serial I/O abstraction" | "Frame transport abstraction" |
| 16-17 | (docstring) mentions COBS | Remove COBS mention from interface contract |

#### 2.2 src/oc/context/IContext.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 14 | `#include <oc/hal/ISerialTransport.hpp>` | `#include <oc/hal/IFrameTransport.hpp>` |
| 273 | `@return Reference to ISerialTransport` | `@return Reference to IFrameTransport` |
| 275 | `hal::ISerialTransport& serial() {` | `hal::IFrameTransport& frames() {` |
| 277 | `assert(apis_->serial && "ISerialTransport not available");` | `assert(apis_->frames && "IFrameTransport not available");` |
| 278 | `return *apis_->serial;` | `return *apis_->frames;` |
| 432 | `@brief Check if ISerialTransport is available` | `@brief Check if IFrameTransport is available` |
| 435 | `bool hasSerial() const { return apis_->serial != nullptr; }` | `bool hasFrames() const { return apis_->frames != nullptr; }` |

#### 2.3 src/oc/context/APIs.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 12 | `class ISerialTransport;` | `class IFrameTransport;` |
| 84 | (comment) "Serial transport API" | "Frame transport API" |
| 85 | `hal::ISerialTransport* serial = nullptr;` | `hal::IFrameTransport* frames = nullptr;` |

#### 2.4 src/oc/context/Requirements.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 28 | `bool serial = false;` | `bool frames = false;` |

#### 2.5 src/oc/context/ContextManager.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 152 | `if (T::REQUIRES.serial && !apis_.serial) {` | `if (T::REQUIRES.frames && !apis_.frames) {` |
| 153 | `"Context requires ISerialTransport but none provided"` | `"Context requires IFrameTransport but none provided"` |

#### 2.6 src/oc/app/OpenControlApp.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 18 | `#include <oc/hal/ISerialTransport.hpp>` | `#include <oc/hal/IFrameTransport.hpp>` |
| 246 | `std::unique_ptr<hal::ISerialTransport> serial_;` | `std::unique_ptr<hal::IFrameTransport> frames_;` |

#### 2.7 src/oc/app/OpenControlApp.cpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 27 | `if (serial_) check(serial_->init(), "Serial");` | `if (frames_) check(frames_->init(), "Frames");` |
| 77-78 | `if (serial_) { serial_->update(); }` | `if (frames_) { frames_->update(); }` |

#### 2.8 src/oc/app/AppBuilder.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 10 | `#include <oc/hal/ISerialTransport.hpp>` | `#include <oc/hal/IFrameTransport.hpp>` |
| 64 | `AppBuilder& serial(std::unique_ptr<hal::ISerialTransport> transport);` | `AppBuilder& frames(std::unique_ptr<hal::IFrameTransport> transport);` |
| 80 | `std::unique_ptr<hal::ISerialTransport> serial_;` | `std::unique_ptr<hal::IFrameTransport> frames_;` |

#### 2.9 src/oc/app/AppBuilder.cpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 20 | `AppBuilder& AppBuilder::serial(std::unique_ptr<hal::ISerialTransport> transport) {` | `AppBuilder& AppBuilder::frames(std::unique_ptr<hal::IFrameTransport> transport) {` |
| 21 | `serial_ = std::move(transport);` | `frames_ = std::move(transport);` |
| 54 | `app.serial_ = std::move(serial_);` | `app.frames_ = std::move(frames_);` |
| 85 | `app.apis_->serial = app.serial_.get();` | `app.apis_->frames = app.frames_.get();` |

### 3. CHANGEMENTS DANS open-control/hal-teensy

#### 3.1 src/oc/hal/teensy/UsbSerial.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 7 | "Implements ISerialTransport" | "Implements IFrameTransport" |
| 15 | `#include <oc/hal/ISerialTransport.hpp>` | `#include <oc/hal/IFrameTransport.hpp>` |
| 34 | `class UsbSerial : public hal::ISerialTransport {` | `class UsbSerial : public hal::IFrameTransport {` |

**Note** : On garde le nom `UsbSerial` pour l'instant (renommage en `CobsSerialTransport` optionnel, phase 2).

#### 3.2 src/oc/hal/teensy/AppBuilder.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 86 | (comment) `// SERIAL` | `// FRAME TRANSPORT` |
| 89 | "Enable USB Serial transport with COBS framing" | "Enable frame transport over USB Serial (COBS framing)" |
| 92 | `AppBuilder& serial() {` | `AppBuilder& frames() {` |
| 93 | `builder_.serial(std::make_unique<UsbSerial>());` | `builder_.frames(std::make_unique<UsbSerial>());` |

#### 3.3 src/oc/teensy/UsbSerial.hpp (duplicate path - même changements)

Mêmes changements que 3.1

#### 3.4 src/oc/teensy/AppBuilder.hpp (duplicate path - même changements)

Mêmes changements que 3.2, lignes différentes :
| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 81 | `AppBuilder& serial() {` | `AppBuilder& frames() {` |
| 82 | `builder_.serial(std::make_unique<UsbSerial>());` | `builder_.frames(std::make_unique<UsbSerial>());` |

### 4. CHANGEMENTS DANS open-control/protocol-codegen

#### 4.1 src/protocol_codegen/generators/protocols/serial8/framing.py

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 56 | `return "oc::hal::ISerialTransport"` | `return "oc::hal::IFrameTransport"` |
| 57 | `return "ISerialTransport"` | `return "IFrameTransport"` |
| 124 | `return '#include <oc/hal/ISerialTransport.hpp>\n'` | `return '#include <oc/hal/IFrameTransport.hpp>\n'` |

#### 4.2 src/protocol_codegen/generators/serial8/cpp/protocol_generator.py

| Ligne | Type | Ancien | Nouveau |
|-------|------|--------|---------|
| 67 | comment | `oc::hal::ISerialTransport&` | `oc::hal::IFrameTransport&` |
| 103 | comment | `#include <oc/hal/ISerialTransport.hpp>` | `#include <oc/hal/IFrameTransport.hpp>` |
| 124 | comment | `Example for ISerialTransport` | `Example for IFrameTransport` |
| 126 | comment | `ISerialTransport& transport` | `IFrameTransport& transport` |
| 181 | comment | `// ISerialTransport` | `// IFrameTransport` |
| 228 | comment | `ISerialTransport& transport_` | `IFrameTransport& transport_` |

### 5. CHANGEMENTS DANS midi-studio/plugin-bitwig

#### 5.1 src/protocol/BitwigProtocol.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 7 | "Uses ISerialTransport for USB Serial" | "Uses IFrameTransport for frame-based communication" |
| 8 | (remove) "The transport layer handles COBS encoding/decoding internally" | (simplify - framing is implementation detail) |
| 30 | `#include <oc/hal/ISerialTransport.hpp>` | `#include <oc/hal/IFrameTransport.hpp>` |
| 44 | "Uses ISerialTransport for COBS-framed serial communication" | "Uses IFrameTransport for frame-based communication" |
| 49 | "Construct protocol with ISerialTransport" | "Construct protocol with IFrameTransport" |
| 53 | "Reference to ISerialTransport" | "Reference to IFrameTransport" |
| 55 | `explicit BitwigProtocol(oc::hal::ISerialTransport& transport)` | `explicit BitwigProtocol(oc::hal::IFrameTransport& transport)` |
| 76 | `oc::hal::ISerialTransport& transport_;` | `oc::hal::IFrameTransport& transport_;` |

#### 5.2 src/context/BitwigContext.hpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 97 | `.midi = true` | `.midi = true,`<br>`.frames = true` |

**Note** : BitwigContext utilise `serial()` mais ne déclare pas `.serial = true`. C'est un bug existant à corriger.

#### 5.3 src/context/BitwigContext.cpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 121 | `protocol_ = std::make_unique<BitwigProtocol>(serial());` | `protocol_ = std::make_unique<BitwigProtocol>(frames());` |

#### 5.4 src/main.cpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 81 | `.serial()` | `.frames()` |

### 6. CHANGEMENTS DANS midi-studio/core

#### 6.1 main.cpp

| Ligne | Ancien | Nouveau |
|-------|--------|---------|
| 69 | `.serial()` | `.frames()` |

**Note** : Ce fichier utilise `.serial()` mais c'est pour le firmware Teensy (pas desktop). À vérifier si nécessaire pour desktop simulator.

### 7. DOCUMENTATION

#### 7.1 open-control/framework/README.md

| Section | Changement |
|---------|------------|
| L63 | `ISerialTransport` → `IFrameTransport` |
| L266-271 | Section "ISerialTransport" → "IFrameTransport", exemples |

#### 7.2 open-control/framework.wiki/HAL-Interfaces.md

| Section | Lignes | Changement |
|---------|--------|------------|
| Header | 302-331 | Rename section, update all references |
| Examples | 491-494 | Update diagrams |

#### 7.3 open-control/.github/profile/README.md

| Lignes | Changement |
|--------|------------|
| 141-142 | `serial()` → `frames()` |

---

## SCRIPTS DE MIGRATION

### Script 1 : Renommage du fichier (Git)

```bash
#!/bin/bash
# run-01-rename-file.sh
# À exécuter depuis open-control/framework/

cd /c/Users/simon/petitechose-audio/open-control/framework
git mv src/oc/hal/ISerialTransport.hpp src/oc/hal/IFrameTransport.hpp
echo "✓ Renamed ISerialTransport.hpp → IFrameTransport.hpp"
```

### Script 2 : Remplacements Framework (sed)

```bash
#!/bin/bash
# run-02-framework-replace.sh
# À exécuter depuis open-control/

FRAMEWORK_DIR="/c/Users/simon/petitechose-audio/open-control/framework"

# Liste des fichiers à modifier
FILES=(
    "src/oc/hal/IFrameTransport.hpp"
    "src/oc/context/IContext.hpp"
    "src/oc/context/APIs.hpp"
    "src/oc/context/Requirements.hpp"
    "src/oc/context/ContextManager.hpp"
    "src/oc/app/OpenControlApp.hpp"
    "src/oc/app/OpenControlApp.cpp"
    "src/oc/app/AppBuilder.hpp"
    "src/oc/app/AppBuilder.cpp"
)

cd "$FRAMEWORK_DIR"

for file in "${FILES[@]}"; do
    if [[ -f "$file" ]]; then
        # Interface name
        sed -i 's/ISerialTransport/IFrameTransport/g' "$file"
        # Include path
        sed -i 's|oc/hal/ISerialTransport\.hpp|oc/hal/IFrameTransport.hpp|g' "$file"
        # Member and method names
        sed -i 's/serial_/frames_/g' "$file"
        sed -i 's/\.serial/\.frames/g' "$file"
        sed -i 's/->serial/->frames/g' "$file"
        sed -i 's/apis_->serial/apis_->frames/g' "$file"
        # Method declarations
        sed -i 's/serial()/frames()/g' "$file"
        sed -i 's/hasSerial/hasFrames/g' "$file"
        echo "✓ Updated $file"
    else
        echo "⚠ File not found: $file"
    fi
done
```

### Script 3 : Remplacements HAL Teensy (sed)

```bash
#!/bin/bash
# run-03-hal-teensy-replace.sh

HAL_TEENSY_DIR="/c/Users/simon/petitechose-audio/open-control/hal-teensy"

FILES=(
    "src/oc/hal/teensy/UsbSerial.hpp"
    "src/oc/hal/teensy/AppBuilder.hpp"
    "src/oc/teensy/UsbSerial.hpp"
    "src/oc/teensy/AppBuilder.hpp"
)

cd "$HAL_TEENSY_DIR"

for file in "${FILES[@]}"; do
    if [[ -f "$file" ]]; then
        sed -i 's/ISerialTransport/IFrameTransport/g' "$file"
        sed -i 's|oc/hal/ISerialTransport\.hpp|oc/hal/IFrameTransport.hpp|g' "$file"
        sed -i 's/builder_\.serial/builder_.frames/g' "$file"
        sed -i 's/AppBuilder& serial()/AppBuilder\& frames()/g' "$file"
        echo "✓ Updated $file"
    else
        echo "⚠ File not found: $file"
    fi
done
```

### Script 4 : Remplacements Protocol Codegen (sed)

```bash
#!/bin/bash
# run-04-codegen-replace.sh

CODEGEN_DIR="/c/Users/simon/petitechose-audio/open-control/protocol-codegen"

FILES=(
    "src/protocol_codegen/generators/protocols/serial8/framing.py"
    "src/protocol_codegen/generators/serial8/cpp/protocol_generator.py"
)

cd "$CODEGEN_DIR"

for file in "${FILES[@]}"; do
    if [[ -f "$file" ]]; then
        sed -i 's/ISerialTransport/IFrameTransport/g' "$file"
        sed -i 's|oc/hal/ISerialTransport\.hpp|oc/hal/IFrameTransport.hpp|g' "$file"
        echo "✓ Updated $file"
    else
        echo "⚠ File not found: $file"
    fi
done
```

### Script 5 : Remplacements Plugin Bitwig (sed)

```bash
#!/bin/bash
# run-05-plugin-bitwig-replace.sh

PLUGIN_DIR="/c/Users/simon/petitechose-audio/midi-studio/plugin-bitwig"

FILES=(
    "src/protocol/BitwigProtocol.hpp"
    "src/context/BitwigContext.cpp"
    "src/main.cpp"
)

cd "$PLUGIN_DIR"

for file in "${FILES[@]}"; do
    if [[ -f "$file" ]]; then
        sed -i 's/ISerialTransport/IFrameTransport/g' "$file"
        sed -i 's|oc/hal/ISerialTransport\.hpp|oc/hal/IFrameTransport.hpp|g' "$file"
        sed -i 's/serial()/frames()/g' "$file"
        echo "✓ Updated $file"
    else
        echo "⚠ File not found: $file"
    fi
done
```

### Script 6 : Remplacements Core (sed)

```bash
#!/bin/bash
# run-06-core-replace.sh

CORE_DIR="/c/Users/simon/petitechose-audio/midi-studio/core"

FILES=(
    "main.cpp"
)

cd "$CORE_DIR"

for file in "${FILES[@]}"; do
    if [[ -f "$file" ]]; then
        sed -i 's/\.serial()/\.frames()/g' "$file"
        echo "✓ Updated $file"
    else
        echo "⚠ File not found: $file"
    fi
done
```

---

## ACTIONS MANUELLES REQUISES

### 1. BitwigContext REQUIRES (CRITIQUE)

**Fichier** : `midi-studio/plugin-bitwig/src/context/BitwigContext.hpp`

**Action** : Ajouter `.frames = true` au REQUIRES

```cpp
// Avant (ligne 94-98)
static constexpr oc::context::Requirements REQUIRES{
    .button = true,
    .encoder = true,
    .midi = true
};

// Après
static constexpr oc::context::Requirements REQUIRES{
    .button = true,
    .encoder = true,
    .midi = true,
    .frames = true
};
```

**Raison** : BitwigContext utilise `frames()` mais ne le déclare pas - bug existant à corriger.

### 2. Mise à jour DocStrings IFrameTransport.hpp

**Fichier** : `open-control/framework/src/oc/hal/IFrameTransport.hpp`

**Action** : Réécrire les commentaires pour refléter la nouvelle sémantique

```cpp
/**
 * @brief Interface for frame-based transport abstraction
 *
 * Provides a platform-agnostic interface for sending and receiving
 * complete binary frames. Used by protocol layers (e.g., BitwigProtocol)
 * for structured communication.
 *
 * Implementations handle their own framing internally:
 * - Stream transports (Serial, UART): use COBS encoding
 * - Datagram transports (UDP): frames are datagrams (no encoding)
 *
 * @note This is for raw frame transport. Protocol encoding
 *       is handled by the protocol layer above.
 */
```

### 3. Documentation README/Wiki

**Action** : Review manuel des fichiers markdown pour cohérence narrative.

**Fichiers** :
- `open-control/framework/README.md` (section IFrameTransport)
- `open-control/framework.wiki/HAL-Interfaces.md` (section complète)
- `open-control/.github/profile/README.md` (exemples)

---

## ORDRE D'EXÉCUTION

```
1. [ ] Créer branche: git checkout -b refactor/frame-transport
2. [ ] Script 1: Renommer fichier (git mv)
3. [ ] Script 2: Framework replacements
4. [ ] Script 3: HAL Teensy replacements
5. [ ] Script 4: Protocol Codegen replacements
6. [ ] Script 5: Plugin Bitwig replacements
7. [ ] Script 6: Core replacements
8. [ ] Manuel 1: BitwigContext REQUIRES
9. [ ] Manuel 2: DocStrings IFrameTransport.hpp
10. [ ] Compilation: Vérifier open-control/framework
11. [ ] Compilation: Vérifier hal-teensy
12. [ ] Compilation: Vérifier midi-studio/plugin-bitwig
13. [ ] Compilation: Vérifier midi-studio/core (desktop)
14. [ ] Grep final: Vérifier aucune occurrence ISerialTransport restante
15. [ ] Manuel 3: Documentation (README, Wiki)
16. [ ] Commit par repo avec message conventionnel
17. [ ] Push et PR
```

---

## VALIDATION POST-MIGRATION

### Grep de vérification

```bash
# Aucun résultat attendu (sauf dans .git, node_modules, etc.)
grep -r "ISerialTransport" --include="*.hpp" --include="*.cpp" --include="*.py" \
    /c/Users/simon/petitechose-audio/open-control \
    /c/Users/simon/petitechose-audio/midi-studio \
    2>/dev/null | grep -v ".git" | grep -v ".pio"
```

### Compilation

```bash
# Framework
cd /c/Users/simon/petitechose-audio/open-control/framework
# (PlatformIO ou CMake selon setup)

# HAL Teensy (via projet qui l'utilise)
cd /c/Users/simon/petitechose-audio/midi-studio/plugin-bitwig
pio run -e dev

# Desktop Simulator
cd /c/Users/simon/petitechose-audio/midi-studio/core/desktop
./build.sh
```

---

## RISQUES ET MITIGATIONS

| Risque | Probabilité | Impact | Mitigation |
|--------|-------------|--------|------------|
| Regex capture trop large | Faible | Moyen | Patterns exacts, pas de wildcards |
| Fichier manqué | Faible | Élevé | Grep exhaustif pré/post |
| Compilation échoue | Moyenne | Faible | Vérifier après chaque script |
| Régénération protocol-codegen | Moyenne | Faible | Regénérer après scripts |
| Templates .inl non couverts | Faible | Moyen | Vérifier BitwigProtocol compile |

---

## PHASE 2 (OPTIONNELLE)

### Renommage des implémentations

| Actuel | Proposé | Raison |
|--------|---------|--------|
| `UsbSerial` | `CobsSerialTransport` | Explicite : COBS + Serial |
| `UsbSerialConfig` | `CobsSerialConfig` | Cohérence |

**Décision** : Reporter en phase 2 pour limiter le scope initial.

---

## CHANGELOG

| Date | Action | Auteur |
|------|--------|--------|
| 2026-01-13 | Création du document | Claude |

