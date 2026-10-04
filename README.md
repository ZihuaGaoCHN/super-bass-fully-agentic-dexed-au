# super-bass-fully-agentic-dexed

Native VST3 and standalone FM synthesizer, derived from Dexed, with an agent for natural-language sound design.

中文输入、自然语言对话、发送 / 回退 / 保存为预设。回退恢复到本轮用户输入前的完整音色。默认检查松键后衰减，只有明确要求才允许无限延音。

## Branches

- `main`: README only.
- `windows`: Windows x64 source and build instructions.
- `apple-silicon-macos`: native Apple Silicon source and build instructions.
- `release`: tested binaries, checksums and validation records.

## Build

Clone the platform branch with `git clone --recurse-submodules --branch windows <repository-url>` (or `apple-silicon-macos`).
See [build instructions](Documentation/BuildingAgenticDexed.md).

Windows: CMake 3.24+, Visual Studio 2022 C++ tools. Configure with `cmake -S . -B build -A x64 -DAGENTIC_DEXED_BUILD_TESTS=ON -DAGENTIC_DEXED_COPY_PLUGIN_AFTER_BUILD=OFF`.

Apple Silicon: Xcode and CMake 3.24+. Configure with `cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64 -DAGENTIC_DEXED_BUILD_TESTS=ON -DAGENTIC_DEXED_COPY_PLUGIN_AFTER_BUILD=OFF`.

Both platforms: `cmake --build build --config Release --target AgenticDexedTests AgenticDexed_VST3 AgenticDexed_Standalone`, then `ctest --test-dir build -C Release --output-on-failure`.

## Credentials

Supply your own provider API key in the app. No credential is shipped. Live-provider tests are opt-in and use a locally saved credential or the `DEEPSEEK_API_KEY` environment variable.

## License and attribution

GPL-3.0. See [LICENSE](LICENSE), [upstream attribution](UPSTREAM.md), and [third-party notices](THIRD_PARTY_NOTICES.md). Dependency source revisions are pinned in Git submodules. Existing author and font-license notices are retained.

## Validation status

Publication candidate; native Windows and Apple Silicon regression must finish before public release. Build success alone is not evidence of native execution. Downloadable packages will carry their own validation record and signing status.
