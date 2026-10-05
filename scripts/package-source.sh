#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
output_root="${repo_root}/dist/source"
version="1.0.1"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output-directory) output_root="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

mkdir -p "${output_root}"
output_root="$(cd "${output_root}" && pwd)"
stage_name="Super-Bass-Fully-Agentic-Dexed-${version}-source"
stage="${output_root}/${stage_name}"
case "${stage}" in "${output_root}"/*) ;; *) echo "Unsafe source stage path" >&2; exit 2 ;; esac
rm -rf "${stage}"
mkdir -p "${stage}"

submodule_status="$(git -C "${repo_root}" submodule status --recursive)"
submodule_count=0
while IFS= read -r line; do
    [[ -z "${line}" ]] && continue
    if [[ "${line:0:1}" != " " ]]; then
        echo "Submodule is not at its pinned initialized commit: ${line}" >&2
        exit 2
    fi
    submodule_count=$((submodule_count + 1))
done <<< "${submodule_status}"
if [[ ${submodule_count} -lt 6 ]]; then
    echo "Expected initialized recursive submodules" >&2
    exit 2
fi

git -c core.autocrlf=false -C "${repo_root}" checkout-index --all --force --prefix="${stage}/"
while IFS= read -r line; do
    [[ -z "${line}" ]] && continue
    remainder="${line:42}"
    path="${remainder%% *}"
    mkdir -p "${stage}/${path}"
    git -c core.autocrlf=false -C "${repo_root}/${path}" checkout-index --all --force --prefix="${stage}/${path}/"
done <<< "${submodule_status}"

root_commit="$(git -C "${repo_root}" rev-parse HEAD)"
manifest="${stage}/source-manifest.json"
{
    printf '{\n  "product": "Super Bass Fully Agentic Dexed",\n  "version": "%s",\n' "${version}"
    printf '  "root_commit": "%s",\n  "submodules": [\n' "${root_commit}"
    index=0
    while IFS= read -r line; do
        [[ -z "${line}" ]] && continue
        sha="${line:1:40}"
        remainder="${line:42}"
        path="${remainder%% *}"
        [[ ${index} -gt 0 ]] && printf ',\n'
        printf '    {"path": "%s", "commit": "%s"}' "${path}" "${sha}"
        index=$((index + 1))
    done <<< "${submodule_status}"
    printf '\n  ],\n  "build_instructions": "Documentation/BuildingAgenticDexed.md"\n}\n'
} > "${manifest}"

rm -f "${output_root}/${stage_name}.zip" "${output_root}/${stage_name}.tar.gz"
(
    cd "${output_root}"
    cmake -E tar cf "${stage_name}.zip" --format=zip "${stage_name}"
    cmake -E tar czf "${stage_name}.tar.gz" "${stage_name}"
)
printf '%s\n' "Created ${output_root}/${stage_name}.zip" \
              "Created ${output_root}/${stage_name}.tar.gz"
rm -rf "${stage}"
