# arqma-storage-server

[![Build Static Binaries](https://github.com/arqma/arqma-storage-server/actions/workflows/build-static-binaries.yml/badge.svg?branch=dev)](https://github.com/arqma/arqma-storage-server/actions/workflows/build-static-binaries.yml)
[![Docker Image CI](https://github.com/arqma/arqma-storage-server/actions/workflows/docker-image.yml/badge.svg)](https://github.com/arqma/arqma-storage-server/actions/workflows/docker-image.yml)

Storage server for Arqma Service Nodes

## Download Pre-built Binaries

Pre-built static binaries are automatically compiled for multiple platforms on every push to the `dev` branch.

**Download from GitHub Actions:**
1. Go to [Actions tab](https://github.com/arqma/arqma-storage-server/actions/workflows/build-static-binaries.yml)
2. Click on the latest successful workflow run
3. Scroll down to "Artifacts" section
4. Download the binary for your platform

**Available platforms:**
- Linux Ubuntu 22.04 (x86_64)
- Linux Ubuntu 24.04 (x86_64)
- macOS ARM64 (M1/M2/M3)
- Windows (x86_64)

**Note:** macOS Intel (x86_64) users should build from source locally.

## Requirements

All required dependencies are downloaded and built automatically as part of the build process.
Platform-specific build modes are included.

### System Requirements

**Ubuntu 22.04 / 24.04:**
```bash
sudo apt-get install -y build-essential cmake git
```

**macOS:**
```bash
# Xcode Command Line Tools required
xcode-select --install
```

## Building from Source

### Quick Start
```bash
git clone https://github.com/arqma/arqma-storage-server.git
cd arqma-storage-server
make release-all
```

The compiled binary will be in the `binaries/` folder.

### Build Options

**Option 1: Static dependencies (recommended)**
```bash
make release-all
```

**Option 2: System libraries (Ubuntu only)**
```bash
mkdir -p build/release && cd build/release
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC_DEPS=OFF ../..
make -j$(nproc)
```

**Debug build:**
```bash
make debug-all
```

## Running the Storage Server

Basic usage:
```bash
./binaries/arqma-storage <public IP> <port> --arqmad-rpc-port <arqmad RPC port>
```

Example:
```bash
./binaries/arqma-storage 0.0.0.0 8080 --arqmad-rpc-port 19994
```

**Important:**
- Replace `0.0.0.0` with your server's public IP
- Ensure ports are open for communication with other storage servers
- The `--arqmad-rpc-port` should match your arqmad daemon's RPC port

For all available options:
```bash
./binaries/arqma-storage --help
```

## API Usage

You can interact with the API using tools like curl or Postman (https://www.getpostman.com/).

### Store Data
```http
POST http://127.0.0.1:8080/store
Content-Type: text/plain

Headers:
  X-Arqma-recipient: "recipient_public_key"
  X-Arqma-ttl: "86400"
  X-Arqma-timestamp: "1540860811000"
  X-Arqma-pow-nonce: "proof_of_work_nonce"

Body:
  "hello world"
```

### Retrieve Data
```http
GET http://127.0.0.1:8080/retrieve

Headers:
  X-Arqma-recipient: "recipient_public_key"
  X-Arqma-last-hash: "" (optional)
```

## Documentation

- **[BUILD_REPORT.md](BUILD_REPORT.md)** - Detailed build analysis and warnings
- **[UBUNTU_COMPATIBILITY.md](UBUNTU_COMPATIBILITY.md)** - Ubuntu 22.04/24.04 compatibility guide
- **[cmake/MACOS_BUILD_FIX.md](cmake/MACOS_BUILD_FIX.md)** - macOS build fixes and technical details

## Platform Support

- ✅ **Ubuntu 22.04 LTS** - Fully supported (pre-built binaries available)
- ✅ **Ubuntu 24.04 LTS** - Fully supported (pre-built binaries available)
- ✅ **macOS ARM64** (M1/M2/M3) - Fully supported (pre-built binaries available)
- ✅ **macOS Intel** (x86_64) - Fully supported (build from source)
- ✅ **Windows** (x86_64) - Fully supported (pre-built binaries available)

## Troubleshooting

### macOS with Homebrew binutils
If you have Homebrew's binutils installed and encounter linker errors, the build system automatically uses native Apple tools. No action required.

### Ubuntu System Libraries
If you prefer using system libraries instead of building static dependencies:
```bash
sudo apt-get install -y libssl-dev libboost-all-dev libsodium-dev libsqlite3-dev
cmake -DBUILD_STATIC_DEPS=OFF ...
```

## License

See [LICENSE](LICENSE) file for details.
