#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
release_root=""
version="1.0.1"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --release-directory) release_root="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [[ -z "${release_root}" || ! -d "${release_root}" ]]; then
    echo "Release directory does not exist: ${release_root}" >&2
    exit 2
fi
release_root="$(cd "${release_root}" && pwd)"
temporary_root="$(mktemp -d "${TMPDIR:-/tmp}/agentic-dexed-scan.XXXXXX")"
trap 'rm -rf "${temporary_root}"' EXIT

canary="sk-agentic-release-""canary-0123456789"
authorization_pattern="Authoriza""tion:[[:space:]]*Bearer[[:space:]]+[A-Za-z0-9._-]+"
private_key_pattern="-----BEGIN[[:space:]]+(RSA[[:space:]]+|EC[[:space:]]+|OPENSSH[[:space:]]+)?PRIVATE[[:space:]]+KEY-----"
workspace_pattern='[A-Za-z]:[/\\][^[:space:]]{0,255}[/\\]\.orca[/\\]workspaces|[A-Za-z]:[/\\]a[/\\][^/\\[:space:]]+[/\\][^/\\[:space:]]+|/(home|Users)/runner/work/'
payload_pattern="unredacted_(request|response)[[:space:]]*[:=]|data:audio/(wav|mpeg)|[\"'](pcm_samples|audio_bytes)[\"'][[:space:]]*:"
scan_patterns=(
    "${canary}"
    "${authorization_pattern}"
    "${private_key_pattern}"
    "${workspace_pattern}"
    "${payload_pattern}"
)
issues_file="${temporary_root}/issues.txt"
: > "${issues_file}"
files_scanned=0

scan_tree() {
    local root="$1"
    local skip_staging="${2:-0}"
    while IFS= read -r -d '' file; do
        if [[ "${skip_staging}" == "1" && "/${file#${root}/}" == *"/Agentic-Dexed-"*"-source/"* ]]; then continue; fi
        if [[ "${skip_staging}" == "1" && "/${file#${root}/}" == *"/Agentic-Dexed-"*"-windows-x64/"* ]]; then continue; fi
        if [[ "${skip_staging}" == "1" && "/${file#${root}/}" == *"/Agentic-Dexed-"*"-macos-universal/"* ]]; then continue; fi
        files_scanned=$((files_scanned + 1))
        relative="${file#${root}/}"
        case "${file}" in
            *.pfx|*.p12|*.pem|*.key) echo "signing credential file: ${relative}" >> "${issues_file}" ;;
        esac
        case "${file}" in
            *.exe|*.dll|*.dylib|*.vst3)
                if [[ "/${relative}" == *"/Agentic-Dexed-"*"-source/"* ]]; then
                    [[ "/${relative}" == *"/Agentic-Dexed-"*"-source/libs/MTS-ESP/libMTS/"* ]] || echo "unexpected executable: ${relative}" >> "${issues_file}"
                else
                    name="$(basename "${file}")"
                    [[ "${name}" == "Agentic Dexed.exe" || "${name}" == "Agentic Dexed.vst3" || "${name}" =~ ^Agentic-Dexed-[0-9.]+-windows-x64-setup\.exe$ ]] || echo "unexpected executable: ${relative}" >> "${issues_file}"
                fi
                ;;
        esac
        matched_sensitive=0
        for pattern in "${scan_patterns[@]}"; do
            if grep -a -E -q -- "${pattern}" "${file}"; then
                matched_sensitive=1
                break
            else
                grep_status=$?
                if [[ ${grep_status} -ne 1 ]]; then
                    echo "Could not scan ${relative}: grep exited ${grep_status}" >&2
                    exit 2
                fi
            fi
        done
        if [[ ${matched_sensitive} -eq 1 ]]; then
            echo "sensitive content in ${relative}" >> "${issues_file}"
        fi
    done < <(find "${root}" -type f -print0)
    while IFS= read -r -d '' link; do
        target="$(cd "$(dirname "${link}")" && realpath "$(readlink "${link}")" 2>/dev/null || true)"
        case "${target}" in "${root}"/*) ;; *) echo "symlink escapes package root: ${link}" >> "${issues_file}" ;; esac
    done < <(find "${root}" -type l -print0)
}

scan_tree "${release_root}" 1
archive_index=0
while IFS= read -r -d '' archive; do
    relative_archive="${archive#${release_root}/}"
    if [[ "/${relative_archive}" == *"/Agentic-Dexed-"*"-source/"* || "/${relative_archive}" == *"/Agentic-Dexed-"*"-windows-x64/"* || "/${relative_archive}" == *"/Agentic-Dexed-"*"-macos-universal/"* ]]; then
        continue
    fi
    archive_index=$((archive_index + 1))
    destination="${temporary_root}/archive-${archive_index}"
    mkdir -p "${destination}"
    case "${archive}" in
        *.zip) ditto -x -k "${archive}" "${destination}" ;;
        *.tar.gz) tar -xzf "${archive}" -C "${destination}" ;;
        *.pkg) pkgutil --expand-full "${archive}" "${destination}/expanded" ;;
    esac
    scan_tree "${destination}"
done < <(find "${release_root}" -type f \( -name '*.zip' -o -name '*.tar.gz' -o -name '*.pkg' \) -print0)

if [[ -s "${issues_file}" ]]; then
    cat "${issues_file}" >&2
    echo "release scan failed" >&2
    exit 3
fi

packages_file="${temporary_root}/packages.txt"
: > "${packages_file}"
while IFS= read -r -d '' package; do
    relative_package="${package#${release_root}/}"
    if [[ "/${relative_package}" == *"/Agentic-Dexed-"*"-source/"* || "/${relative_package}" == *"/Agentic-Dexed-"*"-windows-x64/"* || "/${relative_package}" == *"/Agentic-Dexed-"*"-macos-universal/"* ]]; then
        continue
    fi
    printf '%s\n' "${package}" >> "${packages_file}"
done < <(find "${release_root}" -type f \( -name '*.zip' -o -name '*.tar.gz' -o -name '*.pkg' -o -name '*.dmg' -o -name '*-setup.exe' \) -print0)
LC_ALL=C sort -o "${packages_file}" "${packages_file}"
: > "${release_root}/SHA256SUMS"
while IFS= read -r package; do
    relative="${package#${release_root}/}"
    hash="$(shasum -a 256 "${package}" | awk '{print $1}')"
    printf '%s  %s\n' "${hash}" "${relative}" >> "${release_root}/SHA256SUMS"
done < "${packages_file}"

root_commit="$(git -C "${repo_root}" rev-parse HEAD)"
source_archive="$(find "${release_root}" -type f -name '*-source.zip' -print -quit)"
source_archive="${source_archive##*/}"
manifest="${release_root}/release-manifest.json"
{
    printf '{\n  "product": "Agentic Dexed",\n  "version": "%s",\n' "${version}"
    printf '  "git_commit": "%s",\n  "submodules": [\n' "${root_commit}"
    index=0
    while IFS= read -r line; do
        [[ -z "${line}" ]] && continue
        sha="${line:1:40}"; remainder="${line:42}"; path="${remainder%% *}"
        [[ ${index} -gt 0 ]] && printf ',\n'
        printf '    {"path": "%s", "commit": "%s"}' "${path}" "${sha}"
        index=$((index + 1))
    done < <(git -C "${repo_root}" submodule status --recursive)
    printf '\n  ],\n'
    printf '  "platforms": [{"name":"windows","architectures":["x86_64"]},{"name":"macos","architectures":["x86_64","arm64"]}],\n'
    printf '  "bundle_id": "com.agenticdexed.AgenticDexed",\n  "plugin_code": "AgDx",\n'
    printf '  "source_archive": "%s",\n  "validation_logs": [' "${source_archive}"
    index=0
    while IFS= read -r log; do
        [[ ${index} -gt 0 ]] && printf ','
        printf '"%s"' "${log#${release_root}/}"
        index=$((index + 1))
    done < <(find "${release_root}" -type f -name '*.log' | LC_ALL=C sort)
    printf ']\n}\n'
} > "${manifest}"
printf '{"result":"pass","files_scanned":%d,"archives_scanned":%d}\n' \
    "${files_scanned}" "${archive_index}" > "${release_root}/release-scan-report.json"
echo "Release scan passed: ${files_scanned} files across ${archive_index} archives"
