# Release signing and notarization

Public Agentic Dexed packages are produced only by the tag-triggered
`.github/workflows/release.yml` workflow. Pull-request builds never receive a
signing environment. Checksums are generated from the final signed and scanned
bytes.

## Windows

The release environment supplies these masked secrets:

- `WINDOWS_SIGNING_CERTIFICATE_B64`: base64-encoded code-signing PFX
- `WINDOWS_CERTIFICATE_PASSWORD`: PFX password

The workflow decodes the PFX into the runner's temporary directory and exposes
its path as `WINDOWS_CERTIFICATE_PATH`. `scripts/sign-windows.ps1` signs the
staged VST3 module and optional Standalone executable with SHA-256 file and RFC
3161 timestamp digests. It then rebuilds the portable zip and Inno Setup
installer from those signed files and signs the installer. The script never
prints certificate or password values. `scripts/verify-signatures.ps1` requires
every PE artifact to have a valid Authenticode signature.

## macOS

The protected release environment supplies:

- `MACOS_APPLICATION_CERTIFICATE_B64` and `MACOS_APPLICATION_CERTIFICATE_PASSWORD`
- `MACOS_INSTALLER_CERTIFICATE_B64` and `MACOS_INSTALLER_CERTIFICATE_PASSWORD`
- `MACOS_APPLICATION_IDENTITY` and `MACOS_INSTALLER_IDENTITY`
- `APPLE_ID`, `APPLE_TEAM_ID`, and `APPLE_APP_PASSWORD`

The workflow imports both certificates into an ephemeral keychain and stores a
temporary `notarytool` profile. `scripts/sign-macos.sh` signs inner Mach-O
binaries and then their bundles with hardened runtime, submits the zip for
notarization, staples the VST3 and Standalone bundles, rebuilds the zip, creates
a signed installer package, notarizes it, and staples it. The verification
script runs strict `codesign`, `spctl`, `stapler`, `pkgutil`, and `lipo` checks.

Both signing scripts fail with `missing signing credential: <name>` before
starting a signing tool when required credentials are absent. Verification of
an unsigned fixture fails with `unsigned artifact`.

