#!/bin/bash

# LVGL Binary Font Generator Script
# Génère automatiquement des polices binaires LVGL avec conversion en tableaux C
#
# Usage: ./generate_font.sh [OPTIONS]
#
# Options:
#   -f, --font <path>           Chemin vers le fichier de police TTF (requis)
#   -s, --size <number>         Taille de police en pixels (défaut: 20)
#   -b, --bpp <number>          Bits par pixel: 1, 2, 4, 8 (défaut: 2)
#   -n, --name <string>         Nom du fichier de sortie (défaut: basé sur le nom de la police)
#   -a, --array <string>        Nom de l'array C (défaut: basé sur le nom)
#   -c, --charset <option>      Jeu de caractères: ascii, utf8, custom (défaut: utf8)
#   -r, --range <string>        Plages de caractères personnalisées (pour --charset custom)
#   -o, --output <path>         Dossier de sortie (défaut: répertoire courant)
#   -h, --help                  Afficher cette aide
#
# Exemples:
#   ./generate_font.sh -f Inter.ttf -s 20 -b 4 -c utf8
#   ./generate_font.sh -f Inter.ttf -s 16 -b 2 -c custom -r "0x20-0x7F,0xE9"
#   ./generate_font.sh -f Arial.ttf -s 24 -b 4 -a my_arial_font

set -e  # Arrêt du script en cas d'erreur

# Couleurs pour les messages
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Variables par défaut
FONT_FILE=""
FONT_SIZE=20
BPP=2
OUTPUT_NAME=""
ARRAY_NAME=""
CHARSET="utf8"
CUSTOM_RANGE=""
OUTPUT_DIR="$(pwd)"

# Plages de caractères prédéfinies
ASCII_RANGE="0x20-0x7F"
UTF8_RANGE="0x20-0x7F,0xA0-0xFF,0x100-0x17F,0x1E00-0x1EFF,0x2000-0x206F,0x20A0-0x20CF"

# Fonction d'aide
show_help() {
    echo -e "${BLUE}LVGL Binary Font Generator${NC}"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -f, --font <path>           Chemin vers le fichier de police TTF (requis)"
    echo "  -s, --size <number>         Taille de police en pixels (défaut: 20)"
    echo "  -b, --bpp <number>          Bits par pixel: 1, 2, 4, 8 (défaut: 2)"
    echo "  -n, --name <string>         Nom du fichier de sortie (défaut: basé sur le nom de la police)"
    echo "  -a, --array <string>        Nom de l'array C (défaut: basé sur le nom)"
    echo "  -c, --charset <option>      Jeu de caractères: ascii, utf8, custom (défaut: utf8)"
    echo "  -r, --range <string>        Plages de caractères personnalisées (pour --charset custom)"
    echo "  -o, --output <path>         Dossier de sortie (défaut: répertoire courant)"
    echo "  -h, --help                  Afficher cette aide"
    echo ""
    echo "Jeux de caractères:"
    echo "  ascii    : Caractères ASCII de base (0x20-0x7F)"
    echo "  utf8     : Support UTF-8 étendu (accents, symboles, monétaires)"
    echo "  custom   : Plages personnalisées avec --range"
    echo ""
    echo "Bits par pixel (BPP):"
    echo "  1bpp     : Monochrome (noir/blanc)"
    echo "  2bpp     : 4 niveaux de gris (défaut)"
    echo "  4bpp     : 16 niveaux de gris (antialiasing haute qualité)"
    echo "  8bpp     : 256 niveaux de gris (qualité maximale)"
    echo ""
    echo "Exemples:"
    echo "  $0 -f Inter.ttf -s 20 -b 4 -c utf8"
    echo "  $0 -f Inter.ttf -s 16 -b 2 -c custom -r \"0x20-0x7F,0xE9\""
    echo "  $0 -f Arial.ttf -s 24 -b 4 -a my_arial_font"
    echo ""
    echo "Structure de sortie:"
    echo "  bin/     : Fichiers binaires LVGL (.bin)"
    echo "  data/    : Tableaux C (.c)"
    exit 0
}

# Fonction de log
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Fonction de nettoyage du nom de fichier
clean_filename() {
    echo "$1" | sed 's/[^a-zA-Z0-9_]/_/g' | tr '[:upper:]' '[:lower:]'
}

# Parsing des arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -f|--font)
            FONT_FILE="$2"
            shift 2
            ;;
        -s|--size)
            FONT_SIZE="$2"
            shift 2
            ;;
        -b|--bpp)
            BPP="$2"
            shift 2
            ;;
        -n|--name)
            OUTPUT_NAME="$2"
            shift 2
            ;;
        -a|--array)
            ARRAY_NAME="$2"
            shift 2
            ;;
        -c|--charset)
            CHARSET="$2"
            shift 2
            ;;
        -r|--range)
            CUSTOM_RANGE="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            ;;
        *)
            log_error "Option inconnue: $1"
            echo "Utilisez --help pour voir les options disponibles."
            exit 1
            ;;
    esac
done

# Vérifications des paramètres requis
if [[ -z "$FONT_FILE" ]]; then
    log_error "Le fichier de police est requis. Utilisez -f ou --font."
    exit 1
fi

if [[ ! -f "$FONT_FILE" ]]; then
    log_error "Le fichier de police '$FONT_FILE' n'existe pas."
    exit 1
fi

# Vérification de lv_font_conv
if ! command -v lv_font_conv &> /dev/null; then
    log_error "lv_font_conv n'est pas installé ou non trouvé dans PATH."
    log_error "Installez-le avec: npm install -g lv_font_conv"
    exit 1
fi

# Génération du nom de sortie automatique si non fourni
if [[ -z "$OUTPUT_NAME" ]]; then
    if [[ "$CHARSET" == "custom" ]]; then
        # Pour les ranges custom, demander le contexte à l'utilisateur
        echo -e "${YELLOW}[INPUT]${NC} Range custom détecté. Veuillez spécifier le contexte d'utilisation:"
        echo "Exemples: splash_title, menu_button, status_bar, etc."
        read -p "Contexte: " CONTEXT_NAME

        if [[ -z "$CONTEXT_NAME" ]]; then
            log_error "Le contexte est requis pour les ranges custom."
            exit 1
        fi

        OUTPUT_NAME="${CONTEXT_NAME}"
    else
        # Pour utf8/ascii, utiliser le nom de la fonte
        FONT_BASENAME=$(basename "$FONT_FILE" .ttf)
        FONT_BASENAME=$(basename "$FONT_BASENAME" .otf)
        FONT_BASENAME=$(clean_filename "$FONT_BASENAME")

        # Extraire le poids de la fonte (Bold, Regular, etc.)
        FONT_WEIGHT=""
        if [[ "$FONT_BASENAME" == *"bold"* ]]; then
            FONT_WEIGHT="bold"
        elif [[ "$FONT_BASENAME" == *"regular"* ]]; then
            FONT_WEIGHT="regular"
        elif [[ "$FONT_BASENAME" == *"light"* ]]; then
            FONT_WEIGHT="light"
        fi

        # Format: nomDeLaFonte_taille_poids_Nbpp
        if [[ -n "$FONT_WEIGHT" ]]; then
            OUTPUT_NAME="${FONT_BASENAME}_${FONT_SIZE}_${FONT_WEIGHT}_${BPP}bpp"
        else
            OUTPUT_NAME="${FONT_BASENAME}_${FONT_SIZE}_${BPP}bpp"
        fi
    fi
fi

# Génération du nom d'array automatique si non fourni
if [[ -z "$ARRAY_NAME" ]]; then
    ARRAY_NAME="${OUTPUT_NAME}_bin"
fi

# Détermination de la plage de caractères
case $CHARSET in
    ascii)
        CHAR_RANGE=$ASCII_RANGE
        ;;
    utf8)
        CHAR_RANGE=$UTF8_RANGE
        ;;
    custom)
        if [[ -z "$CUSTOM_RANGE" ]]; then
            log_error "Pour --charset custom, la plage --range est requise."
            exit 1
        fi
        CHAR_RANGE=$CUSTOM_RANGE
        ;;
    *)
        log_error "Jeu de caractères invalide: $CHARSET. Utilisez: ascii, utf8, custom"
        exit 1
        ;;
esac

# Création des dossiers de sortie
BIN_DIR="$OUTPUT_DIR/bin"
DATA_DIR="$OUTPUT_DIR/data"
mkdir -p "$BIN_DIR" "$DATA_DIR"

# Fichiers de sortie
BIN_FILE="$BIN_DIR/${OUTPUT_NAME}.bin"
CPP_FILE="$DATA_DIR/${OUTPUT_NAME}.c.inc"  # .c.inc to prevent individual compilation

log_info "Génération de la police:"
log_info "  Police source : $FONT_FILE"
log_info "  Taille        : ${FONT_SIZE}px"
log_info "  BPP           : ${BPP} bits/pixel"
log_info "  Jeu de chars  : $CHARSET"
log_info "  Plage         : $CHAR_RANGE"
log_info "  Nom array     : $ARRAY_NAME"
log_info "  Fichier bin   : $BIN_FILE"
log_info "  Fichier C++   : $CPP_FILE"

# Génération de la police binaire LVGL
log_info "Génération du fichier binaire LVGL..."
lv_font_conv \
    --font "$FONT_FILE" \
    --size $FONT_SIZE \
    --format bin \
    --bpp $BPP \
    --range "$CHAR_RANGE" \
    --lv-font-name "$ARRAY_NAME" \
    --no-kerning \
    -o "$BIN_FILE"

if [[ ! -f "$BIN_FILE" ]]; then
    log_error "Échec de la génération du fichier binaire."
    exit 1
fi

BIN_SIZE=$(stat -c%s "$BIN_FILE" 2>/dev/null || stat -f%z "$BIN_FILE" 2>/dev/null || echo "?")
log_success "Fichier binaire généré: ${BIN_SIZE} bytes"

# Conversion en tableau C++ avec xxd
log_info "Conversion en tableau C++..."
xxd -i "$BIN_FILE" > "$CPP_FILE"

if [[ ! -f "$CPP_FILE" ]]; then
    log_error "Échec de la conversion en tableau C++."
    exit 1
fi

# Nettoyage du nom de variable dans le fichier C++ (xxd utilise le chemin complet)
# Remplacer le nom de variable généré automatiquement par xxd par le nom souhaité
# Utilisation de PROGMEM pour stocker en Flash et économiser la RAM
sed -i "s/unsigned char [^=]*=/const uint8_t ${ARRAY_NAME}[] PROGMEM =/g" "$CPP_FILE"
sed -i "s/unsigned int [^;]*;/const uint32_t ${ARRAY_NAME}_len = $(stat -c%s "$BIN_FILE" 2>/dev/null || stat -f%z "$BIN_FILE");/g" "$CPP_FILE"

log_success "Tableau C généré: $CPP_FILE (included via binary_font_buffer.cpp)"

# Génération d'un fichier header C++ pur
HEADER_FILE="$DATA_DIR/${OUTPUT_NAME}.hpp"
cat > "$HEADER_FILE" << EOF
#pragma once

#include <Arduino.h>


extern const uint8_t ${ARRAY_NAME}[] PROGMEM;
extern const uint32_t ${ARRAY_NAME}_len;
EOF

log_success "Header généré: $HEADER_FILE"

# Suppression du fichier binaire temporaire (seul le .c.inc est nécessaire)
if [[ -f "$BIN_FILE" ]]; then
    rm "$BIN_FILE"
    log_info "Fichier .bin temporaire supprimé (seul le .c.inc est utilisé)"
fi

# Résumé final
echo ""
log_success "✅ Génération terminée avec succès !"
echo ""
echo -e "${GREEN}Fichiers générés:${NC}"
echo -e "  📄 ${CPP_FILE}"
echo -e "  📄 ${HEADER_FILE}"
echo ""
echo -e "${BLUE}Usage dans le code:${NC}"
echo -e "  #include \"data/${OUTPUT_NAME}.hpp\""
echo -e "  lv_font_t* font = lv_binfont_create_from_buffer(${ARRAY_NAME}, ${ARRAY_NAME}_len);"
echo ""