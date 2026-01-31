# Build Instructions

## Prerequisites

- **MSYS2** with UCRT64 environment
- **CMake** (installed via MSYS2)
- **GCC/Make** (installed via MSYS2)

Install required packages:
```bash
pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc make
```

---

## Build Steps

### 1. Open MSYS2 UCRT64 Terminal

Launch `MSYS2 UCRT64` from Start menu (NOT MinGW64 or MSYS2).

### 2. Navigate to Source Directory

```bash
# Replace with your actual path
cd /c/Users/$USER/.cache/quelpa/build/vterm
```

### 3. Configure Build (First Time Only)

```bash
mkdir -p build
cd build
cmake .. -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_SYSTEM_LIBVTERM=OFF
cd ..
```

### 4. Build

```bash
cmake --build build --target vterm-module -j8
```

**Note**: `-j8` uses 8 parallel jobs. Adjust based on CPU cores (use 3/4 of total).

### 5. Deploy Binaries

```bash
# Create deployment directory if needed
mkdir -p /c/Users/$USER/.cache/vterm

# Copy binaries
cp vterm-module.dll /c/Users/$USER/.cache/vterm/
cp build/conpty-proxy/conpty_proxy.exe /c/Users/$USER/.cache/vterm/
```

### 6. Verify

```bash
ls -lh /c/Users/$USER/.cache/vterm/
```

You should see:
- `vterm-module.dll` (~350KB)
- `conpty_proxy.exe` (~150KB)

---

## Rebuild After Changes

For incremental builds (faster):

```bash
cd /c/Users/$USER/.cache/quelpa/build/vterm
cmake --build build --target vterm-module -j8
cp vterm-module.dll /c/Users/$USER/.cache/vterm/
```

Only rebuild `conpty_proxy.exe` if you modified `conpty-proxy/*.c`:

```bash
cmake --build build --target conpty-proxy -j8
cp build/conpty-proxy/conpty_proxy.exe /c/Users/$USER/.cache/vterm/
```

---

## Clean Build (If Needed)

```bash
cd /c/Users/$USER/.cache/quelpa/build/vterm
rm -rf build
mkdir build
cd build
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DUSE_SYSTEM_LIBVTERM=OFF
cd ..
cmake --build build --target vterm-module -j8
```

---

## Troubleshooting

### CMake Not Found

```bash
pacman -S mingw-w64-ucrt-x86_64-cmake
```

### GCC Not Found

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc make
```

### libvterm Not Building

Ensure submodules are initialized:

```bash
git submodule update --init --recursive
```

### Wrong Architecture

Make sure you're using **UCRT64**, not MinGW64 or MSYS2:
- Check terminal title bar: should say "MSYS2 UCRT64"
- Check GCC: `gcc -dumpmachine` should show `x86_64-w64-mingw32`

---

## Build Targets

- `vterm-module`: Builds `vterm-module.dll` (main target)
- `conpty-proxy`: Builds `conpty_proxy.exe` 
- `libvterm`: Builds vendored libvterm (automatic dependency)

Build all:
```bash
cmake --build build -j8
```

---

## Portable Paths

The build uses portable `$USER` variable. To make it fully portable across machines:

1. Set deployment location via environment variable:
   ```bash
   export VTERM_DEPLOY_DIR=/c/Users/$USER/.cache/vterm
   ```

2. Use in build scripts:
   ```bash
   cp vterm-module.dll $VTERM_DEPLOY_DIR/
   cp build/conpty-proxy/conpty_proxy.exe $VTERM_DEPLOY_DIR/
   ```

3. Or create a build script `build.sh`:
   ```bash
   #!/bin/bash
   DEPLOY_DIR="${VTERM_DEPLOY_DIR:-/c/Users/$USER/.cache/vterm}"
   cmake --build build --target vterm-module -j8
   mkdir -p "$DEPLOY_DIR"
   cp vterm-module.dll "$DEPLOY_DIR/"
   cp build/conpty-proxy/conpty_proxy.exe "$DEPLOY_DIR/"
   echo "Deployed to: $DEPLOY_DIR"
   ```

---

## Build Configuration

Default settings in `CMakeLists.txt`:
- **Release mode**: Optimizations enabled (-O2)
- **Vendored libvterm**: Uses bundled libvterm (not system)
- **Windows only**: ConPTY proxy only builds on Windows

To change build type:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug  # For debugging with symbols
```
