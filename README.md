# Engine

[![CI](https://github.com/YOUR_ORG/YOUR_REPO/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_ORG/YOUR_REPO/actions/workflows/ci.yml)

A DX12/C++20 FPS game engine for Windows.

## Build

```powershell
cmake --preset debug
cmake --build --preset build-debug
```

## Test

```powershell
cd build/debug
ctest -L unit --output-on-failure
```

## Requirements

- Windows 10/11 with DX12 support
- Visual Studio 2022 (MSVC 17+)
- CMake 3.26+
- vcpkg (manifest mode, `C:/vcpkg`)
