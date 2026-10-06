# Super Bass Fully Agentic Dexed — R6

Version 1.0.1, R6 candidate. Native Standalone and VST3 builds for Windows x64 and Apple Silicon macOS.

## Downloads

- [Windows x64 ZIP](./R6/Super-Bass-Fully-Agentic-Dexed-windows-x64-R6.zip)
- [Apple Silicon macOS ARM64 diagnostic bundle](./R6/Super-Bass-Fully-Agentic-Dexed-arm64-R6.tar.gz)
- [GitHub pre-release and complete corresponding source](https://github.com/postenginnering/super-bass-fully-agentic-dexed/releases/tag/v1.0.1-r6)
- [SHA-256 checksums](./R6/SHA256SUMS.txt)

Extract the Windows ZIP and run `Super Bass Fully Agentic Dexed.exe`, or install its VST3 bundle in your VST3 plug-in directory. The macOS package contains native ARM64 app and VST3 bundles plus the regression runner and diagnostic tools; it does not contain application source or user credentials. The Windows build is unsigned; the macOS build is ad-hoc signed and is not notarized.

## Changes

- Windows editor fits the display work area and scales with window size, with separate physical-window and logical-layout preferences.
- Taller amplitude and pitch envelope graphs on both platforms.
- Unified product naming, natural-language Agent conversation, Chinese input, whole-request rollback and complete preset saving.

## Validation and remaining check

- Windows R6: 7/7 CTest suites, 20,543 assertions; VST3 pluginval strictness 8 succeeded.
- Native Mac R6 report: 19,589 runtime assertions passed, none failed; VST3 pluginval strictness 8 succeeded. All three executable checksums match this package.
- Mac R6 real-model test did **not start** because the test process could not obtain the DeepSeek credential. This prerequisite remains outstanding; R6 is a pre-release, not a claim that all native live-model checks passed.
- Earlier Windows R5 real DeepSeek test passed 403 assertions. Agent/model code did not change in R6; that earlier result is not a new R6 live-model run.
- Release package scan found no credentials or private local paths, and internal checksum manifests were verified.

## Source and license

Corresponding application source: commit `41ee5c24c3756b24644b4a30f88af140868615d3` on [windows](https://github.com/postenginnering/super-bass-fully-agentic-dexed/tree/windows) and [apple-silicon-macos](https://github.com/postenginnering/super-bass-fully-agentic-dexed/tree/apple-silicon-macos).

The release asset `Super-Bass-Fully-Agentic-Dexed-1.0.1-source.tar.gz` contains that source and all 14 pinned recursive dependencies. GitHub's automatically generated source archive for the release tag contains the release branch, so use the explicitly named complete source asset to build the application.

The complete source asset clears the public Google API key fields in three upstream JUCE Android demo configuration files. This packaging transformation is recorded in `source-manifest.json`; native application code and Windows/macOS binaries are unchanged.

GNU GPL version 3. See [LICENSE](./LICENSE) and [third-party notices](./THIRD_PARTY_NOTICES.md). API keys are supplied by each user and are not distributed. GitHub Actions remains disabled; these artifacts were built and checked outside Actions.
