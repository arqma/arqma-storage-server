# GitHub Actions Workflows

This directory contains automated CI/CD workflows for the ARQMA Storage Server project.

## Workflows

### 1. Build Static Binaries (`build-static-binaries.yml`)

Automatically builds static binaries for multiple platforms on every push to `dev` or `master` branches.

**Supported Platforms:**
- **Linux Ubuntu 22.04** (x86_64) - Static binary
- **Linux Ubuntu 24.04** (x86_64) - Static binary
- **macOS ARM64** (Apple Silicon M1/M2/M3) - Static binary

**Note:** Windows builds are temporarily disabled pending build system improvements.

**Triggers:**
- Push to `dev` or `master` branch
- Pull requests to `dev` or `master` branch
- Manual workflow dispatch

**Artifacts:**
Each build produces a compressed artifact containing the static binary:
- Linux: `arqma-storage-linux-ubuntu-{version}-x86_64.tar.gz`
- macOS ARM64: `arqma-storage-macos-arm64.tar.gz`

**Releases:**
- **Tags** (e.g., `v1.0.0`): Creates a GitHub Release with all binaries
- **dev branch**: Automatically updates `dev-latest` pre-release tag
- **master branch**: Automatically creates new version tag and release when dev is merged

**Artifact Retention:** 30 days

**Download Artifacts:**
1. Go to Actions tab in GitHub
2. Click on the workflow run
3. Scroll down to "Artifacts" section
4. Download the desired platform binary

### 2. Docker Image CI (`docker-image.yml`)

Builds Docker image on push to `master` branch.

**Triggers:**
- Push to `master` branch
- Pull requests to `master` branch

## Development Workflow

### Testing Changes
1. Push changes to `dev` branch
2. Wait for automated builds to complete
3. Download and test artifacts
4. Create PR to merge `dev` → `master`

### Manual Trigger
You can manually trigger the build workflow:
1. Go to Actions tab
2. Select "Build Static Binaries" workflow
3. Click "Run workflow"
4. Select branch and run

## Build Requirements

Each platform has specific requirements handled automatically by the workflow:

**Linux:**
- build-essential
- cmake
- git
- Builds fully static binaries (BUILD_STATIC_DEPS=ON)

**macOS:**
- Xcode Command Line Tools (pre-installed on runners)
- cmake (installed via Homebrew)
- Builds fully static binaries (BUILD_STATIC_DEPS=ON)

## Platform-Specific Notes

### macOS
The workflow uses:
- `macos-14` for ARM64 (Apple Silicon M1/M2/M3)
- Forces native Apple tools to avoid GNU binutils conflicts
- Intel (x86_64) builds are not supported in CI (users with Intel Macs should build locally)

### Windows (Temporarily Disabled)
Windows builds have been temporarily disabled while build system improvements are being developed.

**Status:** In progress
**Expected:** Future release
**Workaround:** Windows users can build from source locally using WSL2 or MSYS2

### Linux
Builds fully static binaries using the project's static dependency system.

## Troubleshooting

### Build Failures

**Check the workflow logs:**
1. Go to Actions tab
2. Click on the failed workflow
3. Click on the failed job
4. Review the logs

**Common issues:**
- Submodule checkout failures → check `.gitmodules`
- Dependency installation failures → check runner environment
- Build failures → check CMake configuration

### Artifact Issues

If artifacts are missing:
1. Check if the build step completed successfully
2. Verify the artifact upload step didn't fail
3. Check artifact retention period (30 days)

## Adding New Platforms

To add support for a new platform:

1. Add a new job to `build-static-binaries.yml`
2. Configure the appropriate runner (e.g., `ubuntu-latest`, `macos-latest`)
3. Install platform-specific dependencies
4. Run the build command
5. Create and upload artifact

Example structure:
```yaml
build-new-platform:
  name: Build New Platform
  runs-on: platform-runner
  steps:
    - uses: actions/checkout@v4
    - name: Install dependencies
      run: # install commands
    - name: Build
      run: make release-all
    - name: Upload artifact
      uses: actions/upload-artifact@v4
```

## Related Documentation

- [BUILD_REPORT.md](../../BUILD_REPORT.md) - Detailed build analysis
- [UBUNTU_COMPATIBILITY.md](../../UBUNTU_COMPATIBILITY.md) - Ubuntu compatibility
- [cmake/MACOS_BUILD_FIX.md](../../cmake/MACOS_BUILD_FIX.md) - macOS build fixes
