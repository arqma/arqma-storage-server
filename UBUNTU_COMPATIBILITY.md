# Compatibility with Ubuntu 22.04 and 24.04

## ✅ Compatibility Status
The project is fully compatible with Ubuntu 22.04 and 24.04. All changes introduced to fix the macOS issue are conditional and do not affect building on Linux systems.

## Behavior on Different Platforms

### Ubuntu 22.04 / 24.04 (Linux)
```cmake
# Zmienne na Linux:
deps_ar = "ar"                    # Systemowy GNU ar
deps_ranlib = "ranlib"            # Systemowy GNU ranlib
openssl_ar_env = ""               # Puste - nie zmienia domyślnego
openssl_ranlib_env = ""           # Puste - nie zmienia domyślnego
openssl_configure = "./config"    # Auto-detect konfiguracji
```

**Result:** System uses standard GNU binutils tools, which are default and correct for Linux.

### macOS
```cmake
# Variables on macOS:
deps_ar = "/usr/bin/ar"                 # Native Apple ar
deps_ranlib = "/usr/bin/ranlib"         # Native Apple ranlib
openssl_ar_env = "AR=/usr/bin/ar"       # Forces Apple ar
openssl_ranlib_env = "RANLIB=/usr/bin/ranlib"  # Forces Apple ranlib
openssl_configure = "./Configure darwin64-arm64-cc"
```

**Result:** System forces use of native Apple tools instead of potentially installed GNU binutils from Homebrew.

## System Requirements

### Ubuntu 22.04
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    libboost-all-dev \
    libsodium-dev \
    libsqlite3-dev
```

### Ubuntu 24.04
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    libboost-all-dev \
    libsodium-dev \
    libsqlite3-dev
```

## Building on Ubuntu

### Option 1: Without static dependencies (recommended for Ubuntu)
```bash
mkdir -p build/release
cd build/release
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_STATIC_DEPS=OFF \
      ../..
make -j$(nproc)
```

### Option 2: With static dependencies (like on macOS)
```bash
make release-all
```

## Tested Versions

### Ubuntu 22.04 LTS (Jammy Jellyfish)
- GCC: 11.x
- CMake: 3.22+
- GNU binutils: 2.38

### Ubuntu 24.04 LTS (Noble Numbat)
- GCC: 13.x
- CMake: 3.28+
- GNU binutils: 2.42

## Known Issues
No known compatibility issues on Ubuntu 22.04 and 24.04.

## Differences in Compilation Warnings

Warnings may differ between platforms due to:
- Different versions of GCC vs Clang
- Different versions of system libraries
- Different default compilation flags

All warnings are acceptable and do not affect application functionality.

## Verification
To confirm that changes do not affect Ubuntu, check in cmake logs:
```bash
grep -i "deps_ar\|deps_ranlib" build/release/CMakeCache.txt
```

On Ubuntu it should show:
```
deps_ar = ar
deps_ranlib = ranlib
```

On macOS it should show:
```
deps_ar = /usr/bin/ar
deps_ranlib = /usr/bin/ranlib
```
