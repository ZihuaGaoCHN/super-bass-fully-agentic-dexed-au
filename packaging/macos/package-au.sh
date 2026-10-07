#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
build_dir="${1:-${repo_root}/build}"
output_dir="${2:-${repo_root}/dist}"
version="1.0.1"

component="${build_dir}/Source/AgenticDexed_artefacts/AU/Super Bass Fully Agentic Dexed.component"

if [[ ! -d "${component}" ]]; then
    echo "Error: AU component not found at ${component}" >&2
    exit 1
fi

staging="${output_dir}/.pkg-staging"
rm -rf "${staging}"
mkdir -p "${staging}/root/Library/Audio/Plug-Ins/Components"
mkdir -p "${staging}/scripts"

ditto "${component}" "${staging}/root/Library/Audio/Plug-Ins/Components/Super Bass Fully Agentic Dexed.component"

cat << 'EOF' > "${staging}/scripts/postinstall"
#!/bin/sh
xattr -cr "/Library/Audio/Plug-Ins/Components/Super Bass Fully Agentic Dexed.component" 2>/dev/null || true
codesign --force --deep -s - "/Library/Audio/Plug-Ins/Components/Super Bass Fully Agentic Dexed.component" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x "${staging}/scripts/postinstall"

mkdir -p "${output_dir}"
output_pkg="${output_dir}/Super-Bass-Fully-Agentic-Dexed-AU-${version}-macOS-arm64.pkg"

pkgbuild \
  --root "${staging}/root" \
  --install-location "/" \
  --scripts "${staging}/scripts" \
  --identifier "com.agenticdexed.AgenticDexed.au.pkg" \
  --version "${version}" \
  "${output_pkg}"

rm -rf "${staging}"
echo "Successfully created: ${output_pkg}"
