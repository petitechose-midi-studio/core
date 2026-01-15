#!/bin/bash
# ==============================================================================
# build.sh - Build MIDI Studio for WebAssembly
# ==============================================================================
# Single script that handles everything: installs Emscripten if needed,
# creates Windows wrappers, and builds the WASM application.
#
# Works on Windows (Git Bash/MSYS2), Linux, and macOS.
#
# Usage:
#   ./build.sh              # Build
#   ./build.sh clean        # Clean and rebuild  
#   ./build.sh serve        # Build and start local server
#   ./build.sh watch        # Watch mode with hot reload
#   ./build.sh setup        # Only install/setup Emscripten (no build)
#
# Output: bin/wasm/midi_studio_wasm.html
# ==============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
GRAY='\033[0;90m'
NC='\033[0m'
BOLD='\033[1m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WASM_DIR="$SCRIPT_DIR/wasm"
BUILD_DIR="$SCRIPT_DIR/build/wasm"
OUTPUT_DIR="$SCRIPT_DIR/bin/wasm"
EMSDK_DIR="$SCRIPT_DIR/tools/emsdk"
EMSDK_BIN="$EMSDK_DIR/upstream/emscripten"

# ==============================================================================
# Install Emscripten SDK if not present
# ==============================================================================
install_emsdk() {
    if [[ -f "$EMSDK_DIR/emsdk" || -f "$EMSDK_DIR/emsdk.bat" ]]; then
        return 0
    fi
    
    echo -e "${CYAN}Installing Emscripten SDK...${NC}"
    mkdir -p "$SCRIPT_DIR/tools"
    
    cd "$SCRIPT_DIR/tools"
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git
    
    cd emsdk
    
    # Use .bat on Windows, direct script on Unix
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
        ./emsdk.bat install latest
        ./emsdk.bat activate latest
    else
        ./emsdk install latest
        ./emsdk activate latest
    fi
    
    echo -e "${GREEN}Emscripten SDK installed${NC}"
}

# ==============================================================================
# Create bash wrappers for Windows (Git Bash needs these)
# ==============================================================================
create_windows_wrappers() {
    # Only needed on Windows/MSYS/Cygwin
    if [[ "$OSTYPE" != "msys" && "$OSTYPE" != "cygwin" ]]; then
        return 0
    fi
    
    # Check if wrappers already exist and are executable
    if [[ -x "$EMSDK_BIN/emcmake" ]]; then
        # Verify it's our wrapper (not a Windows binary)
        if head -1 "$EMSDK_BIN/emcmake" 2>/dev/null | grep -q "bash"; then
            return 0
        fi
    fi
    
    echo -e "  ${CYAN}→${NC} Creating bash wrappers for Windows..."
    
    for cmd in emcmake emmake emcc em++; do
        cat > "$EMSDK_BIN/$cmd" << EOF
#!/bin/bash
python "\$(dirname "\$0")/$cmd.py" "\$@"
EOF
        chmod +x "$EMSDK_BIN/$cmd"
    done
}

# ==============================================================================
# Setup PATH for Emscripten
# ==============================================================================
setup_path() {
    # Add emsdk to PATH if not already there
    if ! command -v emcc &> /dev/null; then
        export PATH="$EMSDK_BIN:$EMSDK_DIR:$PATH"
        
        # Find node path
        NODE_DIR=$(find "$EMSDK_DIR/node" -maxdepth 1 -type d -name "*_64bit" 2>/dev/null | head -1)
        if [[ -n "$NODE_DIR" ]]; then
            export PATH="$NODE_DIR/bin:$PATH"
        fi
    fi
}

# ==============================================================================
# Check Emscripten is working
# ==============================================================================
check_emscripten() {
    if ! command -v emcc &> /dev/null; then
        echo -e "${RED}Error: emcc not found in PATH${NC}"
        echo "Try running: ./build.sh setup"
        exit 1
    fi
    
    EMCC_VERSION=$(emcc --version 2>/dev/null | head -n1)
    echo -e "  ${CYAN}→${NC} Emscripten: ${CYAN}$EMCC_VERSION${NC}"
}

# ==============================================================================
# Check PlatformIO dependencies
# ==============================================================================
check_pio_deps() {
    CORE_DIR="$SCRIPT_DIR/.."
    if [[ ! -d "$CORE_DIR/.pio/libdeps" ]]; then
        echo -e "  ${CYAN}→${NC} Installing PlatformIO dependencies..."
        cd "$CORE_DIR"
        pio pkg install > /dev/null 2>&1 || {
            echo -e "${RED}Error: Failed to install PlatformIO dependencies${NC}"
            echo "Run manually: cd $CORE_DIR && pio pkg install"
            exit 1
        }
        cd "$SCRIPT_DIR"
    fi
}

# ==============================================================================
# Main
# ==============================================================================

echo ""
echo -e "${BOLD}MIDI Studio - WebAssembly Build${NC}"
echo -e "${GRAY}─────────────────────────────────────────${NC}"

# Handle arguments
CLEAN=false
SERVE=false
WATCH=false
SETUP_ONLY=false

for arg in "$@"; do
    case "$arg" in
        clean)  CLEAN=true ;;
        serve)  SERVE=true ;;
        watch)  WATCH=true ;;
        setup)  SETUP_ONLY=true ;;
    esac
done

# Step 1: Install Emscripten if needed
install_emsdk

# Step 2: Create Windows wrappers if needed
create_windows_wrappers

# Step 3: Setup PATH
setup_path

# Step 4: Verify Emscripten works
check_emscripten

# Exit early if only setup was requested
if $SETUP_ONLY; then
    echo ""
    echo -e "${GREEN}Setup complete!${NC}"
    echo -e "Run ${CYAN}./build.sh${NC} to build the application."
    exit 0
fi

# Step 5: Check PlatformIO deps
check_pio_deps

# Step 6: Clean if requested
if $CLEAN && [[ -d "$BUILD_DIR" ]]; then
    echo -e "  ${CYAN}→${NC} Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Step 7: Create build directory
mkdir -p "$BUILD_DIR"

# ==============================================================================
# CMake Configure
# ==============================================================================
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo -e "  ${CYAN}→${NC} Configuring CMake..."
    
    cd "$BUILD_DIR"
    emcmake cmake "$WASM_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        > cmake_configure.log 2>&1 || {
        echo -e "  ${RED}✗${NC} CMake configure failed"
        echo -e "     ${GRAY}See: $BUILD_DIR/cmake_configure.log${NC}"
        tail -20 "$BUILD_DIR/cmake_configure.log"
        exit 1
    }
    
    echo -e "  ${GREEN}✓${NC} CMake configured"
fi

# ==============================================================================
# Build
# ==============================================================================
echo -e "  ${CYAN}→${NC} Compiling..."

cd "$BUILD_DIR"
emmake make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
    > build.log 2>&1 || {
    echo -e "  ${RED}✗${NC} Build failed"
    echo -e "     ${GRAY}See: $BUILD_DIR/build.log${NC}"
    tail -30 "$BUILD_DIR/build.log"
    exit 1
}

echo -e "  ${GREEN}✓${NC} Build complete"

# ==============================================================================
# Output info
# ==============================================================================
echo ""
if [[ -f "$OUTPUT_DIR/midi_studio_wasm.html" ]]; then
    SIZE_HTML=$(du -h "$OUTPUT_DIR/midi_studio_wasm.html" | cut -f1)
    SIZE_WASM=$(du -h "$OUTPUT_DIR/midi_studio_wasm.wasm" 2>/dev/null | cut -f1 || echo "N/A")
    SIZE_JS=$(du -h "$OUTPUT_DIR/midi_studio_wasm.js" 2>/dev/null | cut -f1 || echo "N/A")
    
    echo -e "${GREEN}Output files:${NC}"
    echo -e "  ${CYAN}→${NC} HTML: bin/wasm/midi_studio_wasm.html ${GRAY}(${SIZE_HTML})${NC}"
    echo -e "  ${CYAN}→${NC} WASM: bin/wasm/midi_studio_wasm.wasm ${GRAY}(${SIZE_WASM})${NC}"
    echo -e "  ${CYAN}→${NC} JS:   bin/wasm/midi_studio_wasm.js ${GRAY}(${SIZE_JS})${NC}"
fi

# ==============================================================================
# Serve if requested
# ==============================================================================
# ==============================================================================
# Watch mode with hot reload
# ==============================================================================
if $WATCH; then
    echo ""
    echo -e "${CYAN}Starting watch mode with hot reload...${NC}"
    echo -e "Open: ${GREEN}http://localhost:8080/midi_studio_wasm.html${NC}"
    echo -e "${GRAY}(Press Ctrl+C to stop)${NC}"
    echo ""
    
    # Create live reload server script
    LIVE_SERVER="$SCRIPT_DIR/tools/live-server.js"
    cat > "$LIVE_SERVER" << 'LIVEJS'
const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');

const PORT = 8080;
const WS_PORT = 8081;
const ROOT = process.argv[2] || '.';

const MIME = {
    '.html': 'text/html', '.js': 'application/javascript',
    '.wasm': 'application/wasm', '.css': 'text/css',
    '.png': 'image/png', '.jpg': 'image/jpeg'
};

// HTTP server
http.createServer((req, res) => {
    let file = path.join(ROOT, req.url === '/' ? 'midi_studio_wasm.html' : req.url);
    if (!fs.existsSync(file)) { res.writeHead(404); res.end('Not found'); return; }
    
    let content = fs.readFileSync(file);
    const ext = path.extname(file);
    
    // Inject live reload script into HTML
    if (ext === '.html') {
        const script = `<script>new WebSocket('ws://localhost:${WS_PORT}').onmessage=()=>location.reload()</script>`;
        content = content.toString().replace('</body>', script + '</body>');
    }
    
    res.writeHead(200, { 
        'Content-Type': MIME[ext] || 'application/octet-stream',
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp'
    });
    res.end(content);
}).listen(PORT, () => console.log(`Server: http://localhost:${PORT}`));

// WebSocket for reload notifications
const wss = new WebSocketServer({ port: WS_PORT });
let clients = [];
wss.on('connection', ws => { clients.push(ws); ws.on('close', () => clients = clients.filter(c => c !== ws)); });

// Watch for reload signal
process.on('SIGUSR1', () => { clients.forEach(c => c.send('reload')); });
fs.watchFile(path.join(ROOT, 'midi_studio_wasm.wasm'), { interval: 500 }, () => {
    console.log('Reloading...');
    clients.forEach(c => c.send('reload'));
});
LIVEJS

    # Check if ws module is available, install if not
    NODE_MODULES="$EMSDK_DIR/node_modules"
    if [[ ! -d "$NODE_MODULES/ws" ]]; then
        echo -e "  ${CYAN}→${NC} Installing WebSocket module..."
        cd "$EMSDK_DIR"
        npm install --silent ws 2>/dev/null || {
            # Fallback: use node from emsdk
            NODE_BIN=$(find "$EMSDK_DIR/node" -name "node" -o -name "node.exe" 2>/dev/null | head -1)
            NPM_BIN=$(dirname "$NODE_BIN")/npm
            "$NPM_BIN" install --silent ws 2>/dev/null
        }
        cd "$SCRIPT_DIR"
    fi
    
    # Kill any existing server on port 8080
    if command -v lsof &> /dev/null; then
        lsof -ti:8080 | xargs kill -9 2>/dev/null || true
    elif command -v netstat &> /dev/null; then
        # Windows: find and kill process using port 8080
        PID=$(netstat -ano 2>/dev/null | grep ":8080 " | grep LISTENING | awk '{print $5}' | head -1)
        [[ -n "$PID" ]] && taskkill //F //PID "$PID" 2>/dev/null || true
    fi
    sleep 1
    
    # Start live server in background
    NODE_BIN=$(find "$EMSDK_DIR/node" -maxdepth 4 -type f \( -name "node" -o -name "node.exe" \) 2>/dev/null | head -1)
    NODE_PATH="$EMSDK_DIR/node_modules" "$NODE_BIN" "$LIVE_SERVER" "$OUTPUT_DIR" &
    SERVER_PID=$!
    sleep 1
    
    # Check if server started successfully
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo -e "${RED}Failed to start server. Port 8080 may be in use.${NC}"
        echo "Try: taskkill //F //IM node.exe (Windows) or killall node (Unix)"
        exit 1
    fi
    
    # Watch directories
    WATCH_DIRS="$SCRIPT_DIR/../src $SCRIPT_DIR/main_wasm.cpp $SCRIPT_DIR/wasm"
    
    echo ""
    echo -e "${GRAY}Watching for changes...${NC}"
    
    # Cleanup on exit
    trap "kill $SERVER_PID 2>/dev/null; exit" INT TERM
    
    # Watch loop
    LAST_HASH=""
    while true; do
        sleep 1
        
        # Compute hash of source files modification times
        HASH=$(find $WATCH_DIRS -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) 2>/dev/null | xargs ls -l 2>/dev/null | md5sum | cut -d' ' -f1)
        
        if [[ "$HASH" != "$LAST_HASH" && -n "$LAST_HASH" ]]; then
            echo ""
            echo -e "${CYAN}Change detected, rebuilding...${NC}"
            
            cd "$BUILD_DIR"
            if emmake make -j$(nproc 2>/dev/null || echo 4) > build.log 2>&1; then
                echo -e "${GREEN}✓${NC} Rebuild complete"
            else
                echo -e "${RED}✗${NC} Build failed"
                tail -10 build.log
            fi
        fi
        
        LAST_HASH="$HASH"
    done
    
    exit 0
fi

if $SERVE; then
    echo ""
    echo -e "${CYAN}Starting local server...${NC}"
    echo -e "Open: ${GREEN}http://localhost:8080/midi_studio_wasm.html${NC}"
    echo -e "${GRAY}(Press Ctrl+C to stop)${NC}"
    echo ""
    
    cd "$OUTPUT_DIR"
    python3 -m http.server 8080 2>/dev/null || python -m http.server 8080
else
    echo ""
    echo -e "${GRAY}To test: ./build.sh serve  |  Hot reload: ./build.sh watch${NC}"
fi
