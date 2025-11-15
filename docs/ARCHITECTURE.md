# BTOON Architecture & Release Strategy

## Repository Structure

BTOON uses a multi-repository architecture to maintain clean separation of concerns and independent versioning for each language ecosystem.

### Core Repository (btoon-core)

The `btoon-core` repository contains:
- Core C++20 implementation
- Format specification
- Reference test vectors
- C API for FFI bindings
- CMake build system
- CLI tools (btoon, btoon-schema, btoon-convert)

### Language Binding Repositories

Each language has its own repository to follow language-specific conventions:

#### Strategy 1: Git Submodules (Recommended for Native Bindings)

For languages that compile against C++ (Python, Node.js, PHP):

```bash
# In btoon-python repository
git submodule add https://github.com/BTOON-project/btoon-core.git core
git submodule update --init --recursive

# Build script references core/
python setup.py build_ext --btoon-core=core/
```

**Advantages:**
- Guaranteed version compatibility
- Single source of truth for C++ code
- Easier CI/CD integration
- Atomic updates across core and bindings

**Implementation:**
```yaml
# .github/workflows/build.yml in btoon-python
- name: Checkout with submodules
  uses: actions/checkout@v3
  with:
    submodules: recursive
    
- name: Build core library
  run: |
    cd core
    mkdir build && cd build
    cmake .. -DBUILD_SHARED_LIBS=ON
    make -j$(nproc)
    
- name: Build Python bindings
  run: |
    pip install pybind11
    python setup.py build_ext --inplace
```

#### Strategy 2: Independent Implementation (Go, JavaScript WebAssembly)

For languages with their own implementations:
- Pure Go implementation following Go idioms
- JavaScript/WebAssembly compiled from C++ but distributed independently
- No direct dependency on btoon-core at runtime

## Release Strategy

### Version Alignment

```
btoon-core: 0.0.1
├── btoon-python: 0.0.1.x (follows core.patch)
├── btoon-nodejs: 0.0.1.x
├── btoon-php: 0.0.1.x
├── btoon-javascript: 0.0.1.x (WebAssembly)
├── btoon-go: 0.0.1 (independent)
└── btoon-spec: 0.0.1
```

### Release Artifacts

#### 1. GitHub Releases Structure

```
btoon-core/
├── releases/
│   └── v0.0.1/
│       ├── binaries/
│       │   ├── linux/
│       │   │   ├── x86_64/
│       │   │   │   ├── btoon
│       │   │   │   ├── btoon-schema
│       │   │   │   ├── btoon-convert
│       │   │   │   └── libbtoon.so.0.0.1
│       │   │   └── aarch64/
│       │   │       └── ...
│       │   ├── darwin/
│       │   │   ├── x86_64/
│       │   │   └── arm64/ (M1/M2)
│       │   └── windows/
│       │       ├── x64/
│       │       │   ├── btoon.exe
│       │       │   └── btoon.dll
│       │       └── x86/
│       ├── headers/
│       │   └── btoon/
│       │       ├── btoon.h
│       │       ├── capi.h
│       │       └── ...
│       ├── wasm/
│       │   ├── btoon.wasm
│       │   └── btoon.js
│       └── checksums.txt
```

#### 2. Build Matrix

**Platforms:**
- Linux: Ubuntu 20.04, 22.04; CentOS 7, 8; Alpine
- macOS: 11 (Big Sur), 12 (Monterey), 13 (Ventura)
- Windows: Server 2019, 2022

**Architectures:**
- x86_64 (amd64)
- aarch64 (arm64)
- armv7 (32-bit ARM)
- wasm32 (WebAssembly)

#### 3. CI/CD Pipeline

```yaml
# .github/workflows/release.yml
name: Release Build

on:
  push:
    tags:
      - 'v*'

jobs:
  build-matrix:
    strategy:
      matrix:
        os: [ubuntu-20.04, ubuntu-22.04, macos-11, macos-12, windows-2019, windows-2022]
        arch: [x64, arm64]
        exclude:
          - os: windows-2019
            arch: arm64
            
    runs-on: ${{ matrix.os }}
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Build
        run: |
          mkdir build
          cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release \
                   -DBUILD_SHARED_LIBS=ON \
                   -DCMAKE_CXX_STANDARD=20
          cmake --build . --config Release
          
      - name: Package
        run: |
          cpack -G TGZ
          
      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: btoon-${{ matrix.os }}-${{ matrix.arch }}
          path: build/btoon-*.tar.gz
```

### Distribution Channels

#### 1. Package Managers

**C++ Libraries:**
- **Conan**: `conan install btoon/0.0.1@`
- **vcpkg**: `vcpkg install btoon`
- **Homebrew**: `brew install btoon`
- **APT/YUM**: Via PPA/COPR repositories

**Language-Specific:**
- **Python**: `pip install btoon`
- **Node.js**: `npm install @btoon/core`
- **Go**: `go get github.com/BTOON-project/btoon-go`
- **PHP**: `pecl install btoon`
- **Ruby**: `gem install btoon`

#### 2. Docker Images

```dockerfile
# Official BTOON images
FROM btoon/btoon:0.0.1         # Full environment
FROM btoon/btoon:0.0.1-alpine  # Minimal
FROM btoon/btoon:0.0.1-dev     # With build tools
```

### Dependency Management

#### For Language Bindings Using Submodules

```python
# setup.py for btoon-python
def get_btoon_core_version():
    """Get version from submodule"""
    with open('core/CMakeLists.txt') as f:
        for line in f:
            if 'project(btoon VERSION' in line:
                version = line.split()[2].rstrip(')')
                return version
    return "0.0.1"

setup(
    name="btoon",
    version=get_btoon_core_version(),
    # ...
)
```

#### Version Locking

```json
// btoon-nodejs/package.json
{
  "name": "@btoon/node",
  "version": "0.0.1",
  "btoonCoreVersion": "0.0.1",
  "scripts": {
    "install": "git submodule update --init --recursive && node-gyp rebuild"
  }
}
```

## Development Workflow

### 1. Feature Development

```bash
# Work on core feature
cd btoon-core
git checkout -b feature/new-codec

# Test with Python bindings
cd ../btoon-python
git submodule update --remote core
python test.py

# Update submodule reference
git add core
git commit -m "Update core to include new codec"
```

### 2. Release Process

```bash
# 1. Tag core release
cd btoon-core
git tag -a v0.0.2 -m "Release 0.0.2"
git push origin v0.0.2

# 2. Update language bindings
cd ../btoon-python
git submodule update --remote
git add core
git commit -m "Update to btoon-core v0.0.2"
git tag -a v0.0.2 -m "Release 0.0.2"
git push origin v0.0.2

# 3. Publish packages
python setup.py sdist bdist_wheel
twine upload dist/*
```

### 3. Continuous Integration

Each language repository has its own CI that:
1. Checks out with submodules
2. Builds btoon-core
3. Builds language bindings
4. Runs language-specific tests
5. Runs cross-compatibility tests

## Binary Compatibility

### ABI Stability

- C API (capi.h) maintains ABI compatibility within major versions
- C++ API may change between minor versions
- Use C API for stable FFI bindings

### Symbol Versioning

```cpp
// Version symbols for shared library
extern "C" {
    BTOON_API const char* btoon_version() {
        return "0.0.1";
    }
    
    BTOON_API int btoon_abi_version() {
        return 1;  // Increment on ABI break
    }
}
```

## Testing Strategy

### Cross-Language Test Suite

```yaml
# test-matrix.yml
test-compatibility:
  matrix:
    producer: [cpp, python, go, javascript]
    consumer: [cpp, python, go, javascript]
  steps:
    - name: Produce test data
      run: ${{ matrix.producer }}/generate-test-data.sh
    
    - name: Consume test data
      run: ${{ matrix.consumer }}/validate-test-data.sh
```

## Security & Signing

### Release Signing

```bash
# Sign releases with GPG
gpg --armor --detach-sign btoon-0.0.1-linux-x64.tar.gz

# Generate checksums
sha256sum btoon-* > SHA256SUMS
gpg --clearsign SHA256SUMS
```

### Reproducible Builds

Use Docker for deterministic builds:

```dockerfile
FROM ubuntu:20.04
RUN apt-get update && apt-get install -y \
    gcc-10 g++-10 cmake git
WORKDIR /build
COPY . .
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc-10 \
    -DCMAKE_CXX_COMPILER=g++-10
RUN cmake --build build
```

## Recommendations

### For BTOON Project:

1. **Use Submodules for Native Bindings**
   - Python, Node.js, PHP should use btoon-core as submodule
   - Ensures consistency and easier maintenance

2. **Independent Implementations for Go**
   - Pure Go implementation for better integration
   - No CGO dependency for easier cross-compilation

3. **WebAssembly for JavaScript**
   - Compile btoon-core to WASM
   - Distribute as npm package without native dependencies

4. **Release Automation**
   - GitHub Actions for multi-platform builds
   - Automatic package publishing
   - Docker images for each release

5. **Version Strategy**
   - Core and bindings share major.minor
   - Bindings can have independent patch versions
   - Tag releases consistently across repos

This architecture provides flexibility while maintaining consistency across the BTOON ecosystem.
