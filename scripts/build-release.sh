#!/bin/bash
# Build release binaries for current platform

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-release}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"

echo "Building BTOON Release Binaries"
echo "==============================="
echo "OS: $OS"
echo "Architecture: $ARCH"
echo "Build Directory: $BUILD_DIR"
echo "Install Prefix: $INSTALL_PREFIX"
echo ""

# Clean previous build
if [ "$CLEAN" = "1" ]; then
    echo "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure based on platform
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release"
CMAKE_ARGS="$CMAKE_ARGS -DBUILD_TESTS=ON"
CMAKE_ARGS="$CMAKE_ARGS -DBUILD_TOOLS=ON"
CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"

# Platform-specific configuration
case "$OS" in
    Darwin)
        # macOS
        if [ "$ARCH" = "arm64" ]; then
            # Apple Silicon
            CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_OSX_ARCHITECTURES=arm64"
        elif [ "$UNIVERSAL" = "1" ]; then
            # Universal binary
            CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_OSX_ARCHITECTURES='x86_64;arm64'"
        fi
        ;;
    Linux)
        # Linux
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_FLAGS='-march=native'"
        
        # Enable LTO for smaller binaries
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON"
        ;;
    MINGW*|CYGWIN*|MSYS*)
        # Windows (MinGW/Cygwin/MSYS2)
        CMAKE_ARGS="$CMAKE_ARGS -G 'MSYS Makefiles'"
        ;;
esac

# Add optional features
if [ "$ENABLE_PYTHON" = "1" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DBUILD_PYTHON_BINDINGS=ON"
fi

if [ "$ENABLE_FUZZING" = "1" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DENABLE_FUZZING=ON"
fi

# Configure
echo "Configuring with: cmake $CMAKE_ARGS .."
cmake $CMAKE_ARGS ..

# Build
echo ""
echo "Building..."
cmake --build . --config Release --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Run tests
if [ "$SKIP_TESTS" != "1" ]; then
    echo ""
    echo "Running tests..."
    ctest -C Release --output-on-failure
fi

# Package binaries
echo ""
echo "Packaging binaries..."

PACKAGE_DIR="$BUILD_DIR/package"
mkdir -p "$PACKAGE_DIR"/{bin,lib,include,doc}

# Copy binaries
cp btoon "$PACKAGE_DIR/bin/" 2>/dev/null || true
cp btoon-schema "$PACKAGE_DIR/bin/" 2>/dev/null || true
cp btoon-convert "$PACKAGE_DIR/bin/" 2>/dev/null || true

# Copy libraries
cp libbtoon.a "$PACKAGE_DIR/lib/" 2>/dev/null || true
cp libbtoon_core.a "$PACKAGE_DIR/lib/" 2>/dev/null || true
cp libbtoon.so* "$PACKAGE_DIR/lib/" 2>/dev/null || true
cp libbtoon.dylib "$PACKAGE_DIR/lib/" 2>/dev/null || true

# Copy headers
cp -r "$ROOT_DIR/include/btoon" "$PACKAGE_DIR/include/"

# Copy documentation
cp "$ROOT_DIR/README.md" "$PACKAGE_DIR/doc/"
cp "$ROOT_DIR/LICENSE" "$PACKAGE_DIR/doc/"
cp -r "$ROOT_DIR/docs" "$PACKAGE_DIR/doc/"

# Create version file
cat > "$PACKAGE_DIR/VERSION" << EOF
BTOON Version: 0.0.1
Build Date: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
Platform: $OS-$ARCH
Compiler: $(cmake --version | head -1)
EOF

# Create archive
ARCHIVE_NAME="btoon-0.0.1-$OS-$ARCH"
echo ""
echo "Creating archive: $ARCHIVE_NAME.tar.gz"

cd "$PACKAGE_DIR"
tar -czf "../$ARCHIVE_NAME.tar.gz" .

echo ""
echo "Build complete!"
echo "Archive: $BUILD_DIR/$ARCHIVE_NAME.tar.gz"
echo ""
echo "To install system-wide:"
echo "  sudo cmake --build '$BUILD_DIR' --target install"
echo ""
echo "To use from archive:"
echo "  tar -xzf $ARCHIVE_NAME.tar.gz"
echo "  export PATH=\$PWD/bin:\$PATH"
echo "  export LD_LIBRARY_PATH=\$PWD/lib:\$LD_LIBRARY_PATH"
