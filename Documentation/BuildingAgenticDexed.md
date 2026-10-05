# Building Super Bass Fully Agentic Dexed

Super Bass Fully Agentic Dexed builds as a VST3 instrument and a standalone application on Windows and macOS. The project keeps build-tree artifacts separate from any system plug-in directory unless copying is explicitly enabled.

## Supported build hosts

- Windows 10 22H2 or newer, with Visual Studio 2022 and its Desktop development with C++ workload.
- macOS 11 or newer, with a current Xcode command-line toolchain. Both `x86_64` and `arm64` builds are supported.
- CMake 3.24 or newer and Git.

Clone with the pinned recursive submodules, or initialize them in an existing checkout:

```bash
git submodule update --init --recursive
```

## Windows x64

Run these commands from a Visual Studio 2022 developer shell or a terminal where CMake can find Visual Studio:

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 `
  -DAGENTIC_DEXED_BUILD_TESTS=ON `
  -DAGENTIC_DEXED_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build/windows --config Release --parallel `
  --target AgenticDexedTests AgenticDexed_VST3 AgenticDexed_Standalone
ctest --test-dir build/windows -C Release --output-on-failure
```

The Release artifacts are written to:

- `build/windows/Source/AgenticDexed_artefacts/Release/VST3/Super Bass Fully Agentic Dexed.vst3`
- `build/windows/Source/AgenticDexed_artefacts/Release/Standalone/Super Bass Fully Agentic Dexed.exe`

## macOS x86_64

```bash
cmake -S . -B build/macos-x86_64 -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DAGENTIC_DEXED_BUILD_TESTS=ON \
  -DAGENTIC_DEXED_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build/macos-x86_64 --config Release --parallel \
  --target AgenticDexedTests AgenticDexed_VST3 AgenticDexed_Standalone
ctest --test-dir build/macos-x86_64 -C Release --output-on-failure
```

## macOS arm64

```bash
cmake -S . -B build/macos-arm64 -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DAGENTIC_DEXED_BUILD_TESTS=ON \
  -DAGENTIC_DEXED_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build/macos-arm64 --config Release --parallel \
  --target AgenticDexedTests AgenticDexed_VST3 AgenticDexed_Standalone
ctest --test-dir build/macos-arm64 -C Release --output-on-failure
```

Each macOS build writes `Super Bass Fully Agentic Dexed.vst3` and `Super Bass Fully Agentic Dexed.app` below `build/<architecture>/Source/AgenticDexed_artefacts/Release`.

## Optional plug-in installation

Set `AGENTIC_DEXED_COPY_PLUGIN_AFTER_BUILD=ON` while configuring to let JUCE copy the VST3 bundle into the current user's plug-in directory after a successful build. The default is `OFF`, including in CI, so builds do not modify a developer or runner installation.

## CI workflow validation

The repository workflow runs independent Windows x64, macOS x86_64, and macOS arm64 jobs. Each job builds both formats, runs the processor tests, and uploads test logs and binaries.

If `actionlint` is installed, validate the workflow locally with:

```bash
actionlint .github/workflows/build.yml
```

When `actionlint` is unavailable, GitHub Actions performs the authoritative workflow parse.
