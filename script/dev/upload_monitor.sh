#!/bin/bash
# Build, upload and monitor with clean output

# Find project root (where platformio.ini is located)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
while [[ "$PROJECT_ROOT" != "/" && ! -f "$PROJECT_ROOT/platformio.ini" ]]; do
    PROJECT_ROOT="$(dirname "$PROJECT_ROOT")"
done

if [[ ! -f "$PROJECT_ROOT/platformio.ini" ]]; then
    echo "Error: platformio.ini not found"
    exit 1
fi

cd "$PROJECT_ROOT"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
GRAY='\033[90m'
NC='\033[0m'

# Cursor control
HIDE_CURSOR='\033[?25l'
SHOW_CURSOR='\033[?25h'

# Get default env from platformio.ini
ENV=$(grep -E "^default_envs" platformio.ini | sed 's/default_envs *= *//')
ENV=${ENV:-dev}

# Kill any existing monitor
taskkill //F //IM python.exe 2>/dev/null
sleep 1

clear
echo -e "${BOLD}MIDI Studio${NC} ${GRAY}$ENV${NC}"
echo ""

# Build with timer (hide cursor to prevent jitter)
echo -ne "${HIDE_CURSOR}"
trap "echo -ne '${SHOW_CURSOR}'" EXIT

# Step 1: Build (to get memory stats)
START_TIME=$(date +%s)
pio run -e "$ENV" > /tmp/pio_build.log 2>&1 &
BUILD_PID=$!

SPIN='⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏'
SPIN_IDX=0
while kill -0 $BUILD_PID 2>/dev/null; do
    ELAPSED=$(($(date +%s) - START_TIME))
    printf "\r${GRAY}Building ${SPIN:$SPIN_IDX:1} ${ELAPSED}s${NC}   "
    SPIN_IDX=$(( (SPIN_IDX + 1) % 10 ))
    sleep 0.1
done
wait $BUILD_PID
BUILD_STATUS=$?
BUILD_TIME=$(($(date +%s) - START_TIME))
printf "\r                      \r"

BUILD_OUTPUT=$(cat /tmp/pio_build.log)

# Step 2: Upload (if build succeeded) - nobuild to skip rebuild
if [ $BUILD_STATUS -eq 0 ]; then
    UPLOAD_START=$(date +%s)
    pio run -e "$ENV" -t nobuild -t upload > /tmp/pio_upload.log 2>&1 &
    UPLOAD_PID=$!

    while kill -0 $UPLOAD_PID 2>/dev/null; do
        ELAPSED=$(($(date +%s) - UPLOAD_START))
        printf "\r${GRAY}Uploading ${SPIN:$SPIN_IDX:1} ${ELAPSED}s${NC}   "
        SPIN_IDX=$(( (SPIN_IDX + 1) % 10 ))
        sleep 0.1
    done
    wait $UPLOAD_PID
    UPLOAD_STATUS=$?
    printf "\r                      \r"

    UPLOAD_OUTPUT=$(cat /tmp/pio_upload.log)
    BUILD_OUTPUT="$BUILD_OUTPUT"$'\n'"$UPLOAD_OUTPUT"

    if [ $UPLOAD_STATUS -ne 0 ]; then
        BUILD_STATUS=$UPLOAD_STATUS
    fi
fi

TOTAL_TIME=$(($(date +%s) - START_TIME))
echo -ne "${SHOW_CURSOR}"

# Parse lib_deps from platformio.ini to get actual source paths
declare -A LIB_PATHS
while IFS= read -r line; do
    # Parse: framework=symlink://../../open-control/framework
    if [[ "$line" =~ ^[[:space:]]*([a-zA-Z0-9_-]+)=symlink://(.+)$ ]]; then
        LIB_NAME="${BASH_REMATCH[1]}"
        LIB_REL_PATH="${BASH_REMATCH[2]}"
        LIB_PATHS["$LIB_NAME"]="$PROJECT_ROOT/$LIB_REL_PATH"
    fi
done < <(sed -n "/^\[env:$ENV\]/,/^\[/p" "$PROJECT_ROOT/platformio.ini" | grep -E "symlink://")

# Extract and display dependency graph with clickable links
echo -e "${GRAY}Dependencies${NC}"
echo "$BUILD_OUTPUT" | grep -E "^\|--" | head -10 | while IFS= read -r line; do
    # Extract lib name (e.g., "framework" from "|-- framework @ 0.1.2")
    LIB_NAME=$(echo "$line" | sed 's/|-- //' | cut -d' ' -f1)
    LIB_VERSION=$(echo "$line" | grep -oP '@ \K[0-9.]+')
    LIB_PATH="${LIB_PATHS[$LIB_NAME]}"

    # OSC 8 hyperlink: \033]8;;URL\033\\TEXT\033]8;;\033\\
    if [[ -n "$LIB_PATH" && -d "$LIB_PATH" ]]; then
        WIN_PATH=$(cygpath -w "$LIB_PATH" 2>/dev/null || echo "$LIB_PATH")
        # vscode://file/PATH?windowId=_blank forces new window
        VSCODE_URI="vscode://file/${WIN_PATH}?windowId=_blank"
        printf "  ${GRAY}│ \033]8;;%s\033\\%s\033]8;;\033\\ @ %s${NC}\n" "$VSCODE_URI" "$LIB_NAME" "$LIB_VERSION"
    else
        printf "  ${GRAY}│ %s @ %s${NC}\n" "$LIB_NAME" "$LIB_VERSION"
    fi
done
echo ""

# Extract memory info from teensy_size output
draw_bar() {
    local pct=$1
    local width=16
    local filled=$((pct * width / 100))
    local empty=$((width - filled))
    local bar=""
    for ((i=0; i<filled; i++)); do bar+="█"; done
    for ((i=0; i<empty; i++)); do bar+="░"; done
    echo "$bar"
}

FLASH_LINE=$(echo "$BUILD_OUTPUT" | grep "teensy_size:.*FLASH:")
RAM1_LINE=$(echo "$BUILD_OUTPUT" | grep "teensy_size:.*RAM1:")
RAM2_LINE=$(echo "$BUILD_OUTPUT" | grep "teensy_size:.*RAM2:")
EXTRAM_LINE=$(echo "$BUILD_OUTPUT" | grep "teensy_size:.*EXTRAM:")

if [ -n "$FLASH_LINE" ]; then
    echo -e "${GRAY}Memory${NC}"

    # FLASH: code + data + headers
    FLASH_CODE=$(echo "$FLASH_LINE" | grep -oP "code:\K[0-9]+")
    FLASH_DATA=$(echo "$FLASH_LINE" | grep -oP "data:\K[0-9]+")
    FLASH_HDR=$(echo "$FLASH_LINE" | grep -oP "headers:\K[0-9]+")
    FLASH_FREE=$(echo "$FLASH_LINE" | grep -oP "free for files:\K[0-9]+")
    FLASH_USED=$((FLASH_CODE + FLASH_DATA + FLASH_HDR))
    FLASH_TOTAL=$((FLASH_USED + FLASH_FREE))
    FLASH_PCT=$((FLASH_USED * 100 / FLASH_TOTAL))
    FLASH_KB=$((FLASH_USED / 1024))
    FLASH_TOTAL_MB=$(awk "BEGIN {printf \"%.1f\", $FLASH_TOTAL/1024/1024}")
    BAR=$(draw_bar $FLASH_PCT)
    echo -e "  ${GRAY}FLASH ${BAR} ${FLASH_KB}KB/${FLASH_TOTAL_MB}MB (${FLASH_PCT}%)${NC}"

    # RAM1: variables + code + padding
    if [ -n "$RAM1_LINE" ]; then
        RAM1_VARS=$(echo "$RAM1_LINE" | grep -oP "variables:\K[0-9]+" | head -1 | tr -d '\r\n')
        RAM1_CODE=$(echo "$RAM1_LINE" | grep -oP "code:\K[0-9]+" | head -1 | tr -d '\r\n')
        RAM1_PAD=$(echo "$RAM1_LINE" | grep -oP "padding:\K[0-9]+" | head -1 | tr -d '\r\n')
        RAM1_FREE=$(echo "$RAM1_LINE" | grep -oP "free for local variables:\K[0-9]+" | head -1 | tr -d '\r\n')
        if [ -n "$RAM1_VARS" ] && [ -n "$RAM1_CODE" ] && [ -n "$RAM1_PAD" ] && [ -n "$RAM1_FREE" ]; then
            RAM1_USED=$((RAM1_VARS + RAM1_CODE + RAM1_PAD))
            RAM1_TOTAL=$((RAM1_USED + RAM1_FREE))
            RAM1_PCT=$((RAM1_USED * 100 / RAM1_TOTAL))
            RAM1_KB=$((RAM1_USED / 1024))
            RAM1_TOTAL_KB=$((RAM1_TOTAL / 1024))
            BAR=$(draw_bar $RAM1_PCT)
            echo -e "  ${GRAY}RAM1  ${BAR} ${RAM1_KB}KB/${RAM1_TOTAL_KB}KB (${RAM1_PCT}%)${NC}"
        fi
    fi

    # RAM2: variables
    if [ -n "$RAM2_LINE" ]; then
        RAM2_VARS=$(echo "$RAM2_LINE" | grep -oP "variables:\K[0-9]+" | head -1 | tr -d '\r\n')
        RAM2_FREE=$(echo "$RAM2_LINE" | grep -oP "free for malloc/new:\K[0-9]+" | head -1 | tr -d '\r\n')
        if [ -n "$RAM2_VARS" ] && [ -n "$RAM2_FREE" ]; then
            RAM2_USED=$RAM2_VARS
            RAM2_TOTAL=$((RAM2_USED + RAM2_FREE))
            RAM2_PCT=$((RAM2_USED * 100 / RAM2_TOTAL))
            RAM2_KB=$((RAM2_USED / 1024))
            RAM2_TOTAL_KB=$((RAM2_TOTAL / 1024))
            BAR=$(draw_bar $RAM2_PCT)
            echo -e "  ${GRAY}RAM2  ${BAR} ${RAM2_KB}KB/${RAM2_TOTAL_KB}KB (${RAM2_PCT}%)${NC}"
        fi
    fi

    # EXTRAM: external PSRAM (8MB)
    if [ -n "$EXTRAM_LINE" ]; then
        EXTRAM_VARS=$(echo "$EXTRAM_LINE" | grep -oP "variables:\K[0-9]+" | head -1 | tr -d '\r\n')
        if [ -n "$EXTRAM_VARS" ]; then
            EXTRAM_TOTAL=8388608  # 8MB
            EXTRAM_USED=$EXTRAM_VARS
            EXTRAM_PCT=$((EXTRAM_USED * 100 / EXTRAM_TOTAL))
            EXTRAM_KB=$((EXTRAM_USED / 1024))
            EXTRAM_TOTAL_MB=$((EXTRAM_TOTAL / 1024 / 1024))
            BAR=$(draw_bar $EXTRAM_PCT)
            echo -e "  ${GRAY}PSRAM ${BAR} ${EXTRAM_KB}KB/${EXTRAM_TOTAL_MB}MB (${EXTRAM_PCT}%)${NC}"
        fi
    fi
    echo ""
fi

# Show warnings
WARN_COUNT=$(echo "$BUILD_OUTPUT" | grep -c "warning:")
if [ $WARN_COUNT -gt 0 ]; then
    echo -e "${YELLOW}Warnings : ${WARN_COUNT}${NC}"
    echo "$BUILD_OUTPUT" | grep "warning:" | head -5 | while IFS= read -r line; do
        FILE=$(echo "$line" | cut -d: -f1)
        LINE_NUM=$(echo "$line" | cut -d: -f2)
        MSG=$(echo "$line" | sed 's/.*warning: //')
        printf "  ${YELLOW}%s:%s${NC} ${GRAY}%s${NC}\n" "$FILE" "$LINE_NUM" "$MSG"
    done
    if [ $WARN_COUNT -gt 5 ]; then
        echo -e "  ${GRAY}... and $((WARN_COUNT-5)) more${NC}"
    fi
    echo ""
fi

# Show errors
if [ $BUILD_STATUS -ne 0 ]; then
    ERR_COUNT=$(echo "$BUILD_OUTPUT" | grep -c "error:")
    echo -e "${RED}Errors : ${ERR_COUNT}${NC}"
    echo "$BUILD_OUTPUT" | grep "error:" | head -5 | while IFS= read -r line; do
        FILE=$(echo "$line" | cut -d: -f1)
        LINE_NUM=$(echo "$line" | cut -d: -f2)
        MSG=$(echo "$line" | sed 's/.*error: //')
        printf "  ${RED}%s:%s${NC} ${GRAY}%s${NC}\n" "$FILE" "$LINE_NUM" "$MSG"
    done
    if [ $ERR_COUNT -gt 5 ]; then
        echo -e "  ${GRAY}... and $((ERR_COUNT-5)) more${NC}"
    fi
    echo ""
    echo -e "${RED}${BOLD}BUILD FAILED${NC} ${GRAY}${TOTAL_TIME}s${NC}"
    exit 1
fi

# Success
if echo "$BUILD_OUTPUT" | grep -q "Uploading"; then
    echo -e "${GREEN}${BOLD}BUILD OK${NC} ${GRAY}Uploaded in ${TOTAL_TIME}s${NC}"
else
    echo -e "${GREEN}${BOLD}BUILD OK${NC} ${GRAY}${TOTAL_TIME}s${NC}"
fi
echo ""

# Extract port and speed for display
PORT=$(echo "$BUILD_OUTPUT" | grep -oP "Uploading.*?(COM[0-9]+)" | grep -oP "COM[0-9]+")
SPEED=$(grep -E "^monitor_speed" platformio.ini | sed 's/monitor_speed *= *//')
echo -e "${GRAY}Monitor : ${PORT:-auto} @ ${SPEED:-115200}${NC}"
echo -e "${GRAY}─────────────────────────────────${NC}"

sleep 2

# Start monitor directly
exec pio device monitor --quiet
