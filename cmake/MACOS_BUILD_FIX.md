# macOS Build Compatibility Fix

## Problem
On macOS with Homebrew binutils installed, the system preferred GNU `ar` and `ranlib` instead of native Apple tools. GNU binutils creates `.a` archives with a symbol table format incompatible with the macOS linker, causing errors:
```
ld: multiple errors: archive member '/' not a mach-o file in libssl.a
```

## Solution
Added conditional configuration in `cmake/StaticBuild.cmake` that:
- **On macOS**: Forces use of native Apple tools (`/usr/bin/ar` and `/usr/bin/ranlib`)
- **On Linux/Ubuntu**: Uses standard system tools (`ar` and `ranlib`)

## Modified Files
1. `cmake/StaticBuild.cmake` - added conditions for macOS/Linux
2. `cmake/ranlib-wrapper.sh` - created but unused (can be removed)

## Compatibility
✅ **Ubuntu 22.04** - Uses system GNU binutils tools
✅ **Ubuntu 24.04** - Uses system GNU binutils tools  
✅ **macOS** - Forces native Apple tools

## Code
```cmake
# Force using native macOS ar and ranlib instead of GNU binutils versions
if(APPLE)
  set(deps_ar "/usr/bin/ar")
  set(deps_ranlib "/usr/bin/ranlib")
else()
  set(deps_ar "ar")
  set(deps_ranlib "ranlib")
endif()
```

Variables are only used on macOS:
```cmake
if(APPLE)
  set(openssl_ranlib_env RANLIB=${deps_ranlib})
  set(openssl_ar_env AR=${deps_ar})
else()
  set(openssl_configure ./config)
endif()
```

On Linux, `openssl_ar_env` and `openssl_ranlib_env` variables remain empty, so the system uses default tools.
