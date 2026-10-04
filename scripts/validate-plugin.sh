#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
plugin_path=""
strictness=5
timeout_ms=120000
output_dir="${repo_root}/build/validation"
pluginval_path=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --plugin-path) plugin_path="$2"; shift 2 ;;
        --strictness) strictness="$2"; shift 2 ;;
        --timeout-ms) timeout_ms="$2"; shift 2 ;;
        --output-dir) output_dir="$2"; shift 2 ;;
        --pluginval-path) pluginval_path="$2"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ -z "${plugin_path}" || ! -e "${plugin_path}" ]]; then
    echo "Plugin path does not exist: ${plugin_path}" >&2
    exit 2
fi
if [[ ! "${strictness}" =~ ^([1-9]|10)$ ]]; then
    echo "Strictness must be between 1 and 10" >&2
    exit 2
fi
if [[ -z "${pluginval_path}" ]]; then
    pluginval_path="$(bash "${script_dir}/download-pluginval.sh")"
fi
if [[ ! -x "${pluginval_path}" ]]; then
    echo "pluginval executable does not exist: ${pluginval_path}" >&2
    exit 2
fi

mkdir -p "${output_dir}"
console_log="${output_dir}/pluginval-console.log"
set +e
"${pluginval_path}" \
    --strictness-level "${strictness}" \
    --timeout-ms "${timeout_ms}" \
    --output-dir "${output_dir}" \
    --sample-rates "44100,48000,96000" \
    --block-sizes "1,32,64,512,1024" \
    --validate "$(cd "$(dirname "${plugin_path}")" && pwd)/$(basename "${plugin_path}")" \
    2>&1 | tee "${console_log}"
status=${PIPESTATUS[0]}
set -e

if [[ ${status} -ne 0 ]]; then
    echo "pluginval failed with exit code ${status}; see ${console_log}" >&2
    exit "${status}"
fi
if [[ ! -s "${console_log}" ]]; then
    echo "pluginval completed without producing a validation log" >&2
    exit 2
fi
