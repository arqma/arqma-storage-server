# GitHub Actions Workflows

This directory contains automated CI/CD workflows for the ARQMA Storage Server project.

## Workflows

### 1. Create Release PR (`create-release-pr.yml`)

**Purpose:** Creates a Pull Request from `dev` to `master` for controlled release process.

**Trigger:** Manual (workflow_dispatch) from GitHub Actions tab

**Process:**
1. Detects latest version tag (e.g., `v1.0.0`)
2. Calculates new version based on selected bump type:
   - `minor` (default): v1.0.0 → v1.1.0
   - `major`: v1.0.0 → v2.0.0
   - `patch`: v1.0.0 → v1.0.1
3. Creates PR from `dev` to `master`
4. Generates changelog from commits since last release
5. Assigns @malbit as required reviewer

**How to use:**
1. Go to [Actions → Create Release PR](https://github.com/arqma/arqma-storage-server/actions/workflows/create-release-pr.yml)
2. Click "Run workflow"
3. Select version bump type (minor/major/patch)
4. Wait for PR to be created
5. @malbit reviews and approves PR
6. Merge PR to trigger automatic release

### 2. Build Static Binaries (`build-static-binaries.yml`)

Automatically builds static binaries for multiple platforms on every push to `dev` or `master` branches, and on version tag pushes.

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

**Jobs:**
- `build-linux`: Builds for Ubuntu 22.04 and 24.04 (matrix)
- `build-macos`: Builds for macOS ARM64 (M1/M2/M3)
- `create-prerelease`: Creates/updates `dev-latest` pre-release (dev branch only)
- `auto-tag-on-master`: Creates version tag after PR merge to master
- `create-release`: Creates GitHub Release when version tag is pushed

**Release Process:**
1. **dev branch push** → Updates `dev-latest` pre-release
2. **PR merge to master** → Auto-creates version tag (e.g., `v1.1.0`)
3. **Tag push** → Builds binaries and creates GitHub Release

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

## Development & Release Workflow

### Development Cycle
1. **Work on dev branch**
   ```bash
   git checkout dev
   git commit -m "Add feature"
   git push origin dev
   ```
2. **Automatic dev-latest pre-release** is created/updated
3. Test the pre-release binaries

### Creating a Stable Release
1. **Trigger Release PR workflow**
   - Go to Actions → Create Release PR
   - Click "Run workflow"
   - Select version bump type (minor/major/patch)
   
2. **Review Process**
   - PR is created: `dev` → `master`
   - @malbit reviews the changelog
   - @malbit approves and merges PR
   
3. **Automatic Release**
   - After PR merge, tag is created automatically
   - Binaries are built for all platforms
   - GitHub Release is created with all artifacts

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
