#!/bin/bash
# Install BTOON from pre-built binaries

set -e

VERSION="0.0.1"
GITHUB_REPO="BTOON-project/btoon-core"
INSTALL_PREFIX="${PREFIX:-/usr/local}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Detect OS and architecture
detect_platform() {
    OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
    ARCH="$(uname -m)"
    
    case "$OS" in
        linux*)
            OS="linux"
            ;;
        darwin*)
            OS="macos"
            ;;
        mingw*|cygwin*|msys*)
            OS="windows"
            ;;
        *)
            echo -e "${RED}Unsupported OS: $OS${NC}"
            exit 1
            ;;
    esac
    
    case "$ARCH" in
        x86_64|amd64)
            ARCH="x64"
            ;;
        aarch64|arm64)
            ARCH="arm64"
            ;;
        armv7l)
            ARCH="armv7"
            ;;
        i386|i686)
            ARCH="x86"
            ;;
        *)
            echo -e "${RED}Unsupported architecture: $ARCH${NC}"
            exit 1
            ;;
    esac
    
    PLATFORM="${OS}-${ARCH}"
}

# Download and extract binary
download_binary() {
    local url="https://github.com/${GITHUB_REPO}/releases/download/v${VERSION}/btoon-${PLATFORM}.tar.gz"
    local tmp_dir="$(mktemp -d)"
    local archive="$tmp_dir/btoon.tar.gz"
    
    echo -e "${GREEN}Downloading BTOON v${VERSION} for ${PLATFORM}...${NC}"
    
    if command -v curl >/dev/null 2>&1; then
        curl -L -o "$archive" "$url" || {
            echo -e "${RED}Failed to download from: $url${NC}"
            exit 1
        }
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$archive" "$url" || {
            echo -e "${RED}Failed to download from: $url${NC}"
            exit 1
        }
    else
        echo -e "${RED}Neither curl nor wget found. Please install one of them.${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}Extracting...${NC}"
    tar -xzf "$archive" -C "$tmp_dir"
    
    echo "$tmp_dir"
}

# Install binaries
install_binaries() {
    local src_dir="$1"
    
    echo -e "${GREEN}Installing to ${INSTALL_PREFIX}...${NC}"
    
    # Check if we need sudo
    if [ -w "$INSTALL_PREFIX" ]; then
        SUDO=""
    else
        SUDO="sudo"
        echo -e "${YELLOW}Installation requires sudo privileges${NC}"
    fi
    
    # Create directories
    $SUDO mkdir -p "$INSTALL_PREFIX"/{bin,lib,include/btoon}
    
    # Install binaries
    if [ -d "$src_dir/bin" ]; then
        for binary in "$src_dir/bin"/*; do
            if [ -f "$binary" ]; then
                $SUDO install -m 755 "$binary" "$INSTALL_PREFIX/bin/"
                echo "  Installed: $(basename "$binary")"
            fi
        done
    fi
    
    # Install libraries
    if [ -d "$src_dir/lib" ]; then
        for lib in "$src_dir/lib"/*; do
            if [ -f "$lib" ]; then
                $SUDO install -m 644 "$lib" "$INSTALL_PREFIX/lib/"
                echo "  Installed: $(basename "$lib")"
            fi
        done
    fi
    
    # Install headers
    if [ -d "$src_dir/include/btoon" ]; then
        $SUDO cp -r "$src_dir/include/btoon"/* "$INSTALL_PREFIX/include/btoon/"
        echo "  Installed: headers"
    fi
    
    # Update library cache on Linux
    if [ "$OS" = "linux" ] && [ -x /sbin/ldconfig ]; then
        $SUDO /sbin/ldconfig
    fi
}

# Verify installation
verify_installation() {
    echo -e "${GREEN}Verifying installation...${NC}"
    
    if command -v btoon >/dev/null 2>&1; then
        echo -e "${GREEN}✓ btoon installed successfully${NC}"
        btoon --version
    else
        echo -e "${YELLOW}⚠ btoon not found in PATH${NC}"
        echo "  Add $INSTALL_PREFIX/bin to your PATH:"
        echo "  export PATH=\"$INSTALL_PREFIX/bin:\$PATH\""
    fi
}

# Main installation flow
main() {
    echo -e "${GREEN}BTOON Installer v${VERSION}${NC}"
    echo "================================"
    echo ""
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --prefix)
                INSTALL_PREFIX="$2"
                shift 2
                ;;
            --version)
                VERSION="$2"
                shift 2
                ;;
            --platform)
                PLATFORM="$2"
                shift 2
                ;;
            --local)
                INSTALL_PREFIX="$HOME/.local"
                shift
                ;;
            --help)
                echo "Usage: $0 [options]"
                echo ""
                echo "Options:"
                echo "  --prefix PATH    Installation prefix (default: /usr/local)"
                echo "  --local          Install to ~/.local (no sudo required)"
                echo "  --version VER    Version to install (default: ${VERSION})"
                echo "  --platform PLAT  Platform override (default: auto-detect)"
                echo "  --help           Show this help message"
                exit 0
                ;;
            *)
                echo -e "${RED}Unknown option: $1${NC}"
                exit 1
                ;;
        esac
    done
    
    # Detect platform if not specified
    if [ -z "$PLATFORM" ]; then
        detect_platform
    fi
    
    echo "Platform: $PLATFORM"
    echo "Install prefix: $INSTALL_PREFIX"
    echo ""
    
    # Download and install
    tmp_dir=$(download_binary)
    install_binaries "$tmp_dir"
    
    # Clean up
    rm -rf "$tmp_dir"
    
    # Verify
    verify_installation
    
    echo ""
    echo -e "${GREEN}Installation complete!${NC}"
    echo ""
    echo "To get started:"
    echo "  btoon --help"
    echo "  btoon-schema --help"
    echo "  btoon-convert --help"
}

# Run main function
main "$@"
