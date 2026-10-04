# Agentic Dexed Phase 5: Cross-Platform Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the completed synth, Agent harness, and pixel editor into validated, signed Windows/macOS VST3 release artifacts with reproducible evidence and user documentation.

**Architecture:** Extend the existing build workflow with pinned plugin validation, end-to-end fixtures, packaging, optional secret-gated signing/notarization, recursive source packaging, and a release manifest. Keep PR validation free of signing secrets; make the tagged release workflow the only producer of public packages.

**Tech Stack:** CMake/CTest, GitHub Actions, pluginval v1.0.4, Inno Setup, Apple codesign/notarytool/productbuild, Windows SignTool, REAPER 7 manual smoke tests.

**Spec:** `docs/superpowers/specs/2026-10-01-agentic-dexed-design.md`

## Global Constraints

- Release Windows 10 22H2+ x64 VST3 and macOS 11+ Universal x86_64/arm64 VST3.
- Keep Standalone in developer/diagnostic artifacts.
- Public packages must be signed for their platform; macOS packages must also be notarized and stapled.
- Use pinned pluginval `v1.0.4`; do not download an unversioned `latest` asset.
- Never expose API keys or signing secrets in logs, artifacts, caches, crash text, or test fixtures.
- Distribute the GPL-3.0 corresponding source, including the exact content of every submodule used for the binaries.

## Review Focus

- **Architecture packaging errors:** CI must inspect the macOS bundle with `lipo` and the Windows PE architecture before publishing.
- **State/automation host failures:** pluginval plus REAPER 7 smoke checks must cover editor open/close, parameter fuzz, state restore, and repeated unload.
- **Secret exposure:** artifact scanning must reject known canary keys, certificate material, authorization headers, and CI temp paths.
- **Incomplete GPL source:** the source archive verifier must prove every submodule directory contains the pinned commit content rather than an empty gitlink.
- **Signing gaps:** release verification must reject unsigned binaries, unstapled macOS packages, or checksums that do not match final post-signing bytes.

---

### Task 1: Pin plugin validators and add repeatable validation scripts

**Files:**
- Create: `tools/pluginval/VERSION`
- Create: `tools/pluginval/README.md`
- Create: `scripts/download-pluginval.ps1`
- Create: `scripts/download-pluginval.sh`
- Create: `scripts/validate-plugin.ps1`
- Create: `scripts/validate-plugin.sh`
- Create: `Tests/cmake/VerifyValidationTools.cmake`
- Modify: `.github/workflows/build.yml`

**Interfaces:**
- Consumes: built `Agentic Dexed.vst3` bundle.
- Produces: pinned pluginval v1.0.4 download/cache, strictness-5 PR logs, strictness-8 release logs, and a CMake verification of versioned URLs.

- [ ] **Step 1: Write the failing tool-pin check**

Assert `tools/pluginval/VERSION` equals `v1.0.4`, both download scripts contain `/releases/download/v1.0.4/`, and neither contains `/latest/` or `latest_release`.

- [ ] **Step 2: Run and verify failure**

Run: `cmake -P Tests/cmake/VerifyValidationTools.cmake`

Expected: FAIL because the pin/scripts do not exist.

- [ ] **Step 3: Implement downloads and headless validation**

Download official `pluginval_Windows.zip` or `pluginval_macOS.zip`, cache by version/platform, and run out of process with explicit plugin path, timeout, output directory, sample rates 44100/48000/96000, block sizes 1/32/64/512/1024, and requested strictness. Reject a missing plugin path before invoking pluginval.

- [ ] **Step 4: Add PR validation jobs**

After build/tests, run pluginval strictness 5 and upload its logs even on failure. Keep the Windows and macOS jobs independent.

- [ ] **Step 5: Run the Windows validator locally**

Run:

```powershell
./scripts/download-pluginval.ps1
./scripts/validate-plugin.ps1 -PluginPath "./build/windows/Source/AgenticDexed_artefacts/Release/VST3/Agentic Dexed.vst3" -Strictness 5
```

Expected: exit 0 and a non-empty validation log.

- [ ] **Step 6: Commit**

```bash
git add tools/pluginval scripts Tests/cmake/VerifyValidationTools.cmake .github/workflows/build.yml
git commit -m "test: validate VST3 artifacts with pinned pluginval"
```

### Task 2: Add end-to-end Agent and realtime-safety gates

**Files:**
- Create: `Tests/AgentEndToEndTests.cpp`
- Create: `Tests/RealtimeSafetyTests.cpp`
- Create: `Tests/fixtures/e2e/*.json`
- Create: `Documentation/ValidationMatrix.md`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: complete parameter, Agent, audition, UI, and processor stack.
- Produces: three required Agent scenarios, teardown stress, and explicit realtime-safety evidence.

- [ ] **Step 1: Write the failing end-to-end scenarios**

Use the local fake provider to prove:

```text
init voice -> requested target -> at least one valid committed transaction -> non-silent finite audition
existing voice -> requested refinement -> bounded diff -> undo/redo exact round trip
silent or clipped voice -> analysis flags problem -> corrective transaction -> analysis clears problem
```

Assert every tool call uses current revisions and all loops stop within four iterations. Capture every fake-provider request and prove it contains no raw/encoded audio, MIDI file, DAW state, local-file contents, or local path.

- [ ] **Step 2: Write realtime and teardown stress tests**

Instrument audio callbacks for allocation, file/network entry, blocking-lock acquisition, and non-finite output. Run parameter fuzz, Agent cancellation, editor churn, state restore, sample-rate/block-size changes, and 1,000 processor create/destroy cycles.

- [ ] **Step 3: Run and fix the complete suite**

Run:

```bash
cmake --build build/windows --config Release --target AgenticDexedTests
ctest --test-dir build/windows -C Release --output-on-failure
```

Expected: all three E2E scenarios and realtime/teardown tests PASS.

- [ ] **Step 4: Record the automated validation matrix**

Map every acceptance criterion to a test name, CI job, pluginval log, screenshot set, or manual host record. An empty evidence cell fails release review.

- [ ] **Step 5: Commit**

```bash
git add Tests/AgentEndToEndTests.cpp Tests/RealtimeSafetyTests.cpp Tests/fixtures/e2e Tests/CMakeLists.txt Documentation/ValidationMatrix.md
git commit -m "test: add Agentic Dexed release gates"
```

### Task 3: Package Windows and macOS artifacts

**Files:**
- Create: `packaging/windows/AgenticDexed.iss`
- Create: `packaging/macos/package.sh`
- Create: `packaging/macos/entitlements.plist`
- Create: `packaging/PackageManifest.cmake`
- Create: `scripts/package-windows.ps1`
- Create: `scripts/package-macos.sh`
- Create: `Tests/cmake/VerifyPackageLayout.cmake`
- Modify: `assets/installers/installer.cmake`

**Interfaces:**
- Consumes: Release VST3/Standalone builds.
- Produces: Windows installer/portable zip and macOS signed-package staging layout, each with license, notices, version, and uninstall metadata.

- [ ] **Step 1: Write the failing package-layout check**

Require one correctly named VST3 bundle, optional Standalone diagnostic binary, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README`, and version manifest. Reject original `Dexed` bundle IDs/codes and duplicate plugin locations.

- [ ] **Step 2: Implement Windows packaging**

Install VST3 into the standard common VST3 directory, put Standalone in an explicit optional component, and provide clean uninstall. Build a portable zip from the same staged files.

- [ ] **Step 3: Implement macOS Universal packaging**

Combine or build the x86_64/arm64 bundle, verify both architectures with `lipo -info`, preserve bundle structure/resources, and stage a `.pkg` plus zip without signing in ordinary developer runs.

- [ ] **Step 4: Run unsigned developer packaging and verify layout**

Run:

```powershell
./scripts/package-windows.ps1 -BuildDirectory ./build/windows -Unsigned
cmake -DPACKAGE_ROOT=./dist/windows -P Tests/cmake/VerifyPackageLayout.cmake
```

Expected: PASS with deterministic file names and no system installation side effect.

- [ ] **Step 5: Commit**

```bash
git add packaging scripts/package-windows.ps1 scripts/package-macos.sh Tests/cmake/VerifyPackageLayout.cmake assets/installers/installer.cmake
git commit -m "build: package Windows and macOS releases"
```

### Task 4: Add secret-gated signing and notarization

**Files:**
- Create: `scripts/sign-windows.ps1`
- Create: `scripts/sign-macos.sh`
- Create: `scripts/verify-signatures.ps1`
- Create: `scripts/verify-signatures.sh`
- Create: `Documentation/ReleaseSigning.md`
- Modify: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: staged unsigned artifacts and CI-provided signing credentials.
- Produces: signed Windows binaries/installer, signed/notarized/stapled macOS VST3/package, and verification logs.

- [ ] **Step 1: Implement signing scripts with explicit missing-secret failure**

Windows uses SignTool with SHA-256 file and timestamp digests. macOS signs nested binaries then bundles with hardened runtime, submits the final archive with `notarytool`, waits for acceptance, and staples/verifies the package. Scripts must never print secret arguments.

- [ ] **Step 2: Implement signature verification**

Use `Get-AuthenticodeSignature`/SignTool verification on Windows and `codesign --verify --deep --strict`, `spctl`, and `stapler validate` on macOS. A public-release job cannot downgrade failure to warning.

- [ ] **Step 3: Add the tag-only release workflow**

Trigger on version tags, build from a clean recursive checkout, run all tests plus pluginval strictness 8, package, sign, notarize, verify, then pass final bytes to checksum/source packaging. PR jobs must have no access to signing environments.

- [ ] **Step 4: Validate scripts without real credentials**

Run each script in verification/dry-run mode against unsigned fixtures. Expected: deterministic `missing signing credential` for sign operations and deterministic `unsigned artifact` for verification, with no secret-like content in logs.

- [ ] **Step 5: Commit**

```bash
git add scripts/sign-* scripts/verify-signatures.* Documentation/ReleaseSigning.md .github/workflows/release.yml
git commit -m "build: sign and notarize release artifacts"
```

### Task 5: Package corresponding source and scan every artifact

**Files:**
- Create: `scripts/package-source.ps1`
- Create: `scripts/package-source.sh`
- Create: `scripts/scan-release.ps1`
- Create: `scripts/scan-release.sh`
- Create: `Tests/cmake/VerifySourceBundle.cmake`
- Create: `THIRD_PARTY_NOTICES.md`
- Modify: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: signed binary packages, repository, initialized submodules, and final version.
- Produces: recursive source archive, notices, SHA-256 manifest, secret-scan report, and release manifest linking binaries to source commit.

- [ ] **Step 1: Write the failing source-bundle verifier**

Extract a test archive and assert root source, design/plan docs, GPL license, `.gitmodules`, and representative files from all six pinned submodules exist. Assert manifest commit/submodule SHAs match the checked-out tree.

- [ ] **Step 2: Implement recursive source packaging**

Copy tracked files and initialized submodule worktrees into a versioned staging directory without `.git`, build zip/tar archives, and include exact root/submodule commit manifest plus build instructions.

- [ ] **Step 3: Implement release scanning**

Scan extracted binary/source packages and logs for the canary API key, `Authorization: Bearer`, signing-certificate extensions/material, workspace paths, and unredacted request/response fixtures. Also reject unexpected executables or symlinks outside the package root.

- [ ] **Step 4: Generate checksums only after signing and scanning**

Create `SHA256SUMS` for final immutable artifacts and a machine-readable `release-manifest.json` containing product version, git commit, submodule SHAs, platform, architectures, bundle ID, plugin code, validation logs, and source archive name.

- [ ] **Step 5: Run verifier/scanner and commit**

Run:

```bash
cmake -DSOURCE_ARCHIVE=<generated-source-archive> -P Tests/cmake/VerifySourceBundle.cmake
./scripts/scan-release.ps1 -ReleaseDirectory ./dist
```

Expected: PASS.

```bash
git add scripts/package-source.* scripts/scan-release.* Tests/cmake/VerifySourceBundle.cmake THIRD_PARTY_NOTICES.md .github/workflows/release.yml
git commit -m "build: publish GPL-complete release bundles"
```

### Task 6: Complete Windows/macOS REAPER 7 smoke validation

**Files:**
- Create: `Documentation/host-tests/REAPER7-Smoke-Test.md`
- Create when executed: `Documentation/host-tests/results/<version>-windows.md`
- Create when executed: `Documentation/host-tests/results/<version>-macos.md`

**Interfaces:**
- Consumes: final signed release candidate installed on clean Windows/macOS test accounts.
- Produces: reproducible manual evidence for host behaviors automation cannot fully establish.

- [ ] **Step 1: Write the exact smoke checklist**

Cover scan/load, MIDI play, every engine, program/cartridge/SysEx, SCL/KBM, MIDI mapping, parameter automation write/read, save/close/reopen project, UI resize/collapse, key store/forget, Responses connection, compatible connection, live/proposed Agent edits, cancel, undo/redo, conflict, offline audition, editor close during request, plugin unload, and uninstall.

- [ ] **Step 2: Execute on Windows REAPER 7**

Record OS/build/package checksums, REAPER version, each result, screenshots/log locations, and any deviation. A failed required item blocks release.

- [ ] **Step 3: Execute on macOS REAPER 7 on both architectures**

Run natively on Apple Silicon and under an x86_64-capable validation environment. Record Gatekeeper/notarization behavior and Keychain prompts in addition to the common checklist.

- [ ] **Step 4: Commit completed evidence**

```bash
git add Documentation/host-tests
git commit -m "test: record cross-platform host validation"
```

### Task 7: Perform the requirement-by-requirement release audit

**Files:**
- Create: `Documentation/releases/<version>-audit.md`
- Modify: `README.md`
- Modify: `Documentation/ValidationMatrix.md`

**Interfaces:**
- Consumes: the approved design, all phase plans, CI/test/pluginval/host/signing/source evidence.
- Produces: a release audit with one authoritative evidence link per explicit requirement and no unverified completion claims.

- [ ] **Step 1: Update user documentation**

Document installation, first launch, secure BYOK setup, supported protocols, live/confirmation modes, Agent examples, diff/undo, privacy boundary, offline behavior, SysEx compatibility, troubleshooting, GPL source, and uninstall.

- [ ] **Step 2: Run the clean final automated gate**

Run:

```bash
cmake -S . -B build/release-audit -G "Visual Studio 17 2022" -A x64 -DAGENTIC_DEXED_BUILD_TESTS=ON -DAGENTIC_DEXED_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build/release-audit --config Release --target AgenticDexedTests AgenticDexed_VST3 AgenticDexed_Standalone
ctest --test-dir build/release-audit -C Release --output-on-failure
./scripts/validate-plugin.ps1 -PluginPath "./build/release-audit/Source/AgenticDexed_artefacts/Release/VST3/Agentic Dexed.vst3" -Strictness 8
git diff --check
```

Expected: every command PASS; the corresponding macOS release workflow is green.

- [ ] **Step 3: Audit each specification requirement**

For every item in sections 1–13 of the design spec, classify the evidence as proven, contradicted, incomplete, weak, or missing. Fix and re-run any item not proven. Include final artifact checksums and exact commit.

- [ ] **Step 4: Commit the completed audit**

```bash
git add README.md Documentation/ValidationMatrix.md Documentation/releases
git commit -m "docs: publish Agentic Dexed release evidence"
```
