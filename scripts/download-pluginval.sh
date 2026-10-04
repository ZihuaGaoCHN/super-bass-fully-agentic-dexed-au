#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
version="$(tr -d '\r\n' < "${repo_root}/tools/pluginval/VERSION")"
cache_root="${1:-${repo_root}/tools/pluginval/cache}"

if [[ "${version}" != "v1.0.4" ]]; then
    echo "Unexpected pluginval version '${version}'; expected v1.0.4" >&2
    exit 2
fi

case "$(uname -s)" in
    Darwin)
        platform="macos"
        asset="pluginval_macOS.zip"
        relative_executable="pluginval.app/Contents/MacOS/pluginval"
        ;;
    *)
        platform="linux"
        asset="pluginval_Linux.zip"
        relative_executable="pluginval"
        ;;
esac

destination="${cache_root}/${version}/${platform}"
archive="${destination}/${asset}"
executable="${destination}/${relative_executable}"
download_url="https://github.com/Tracktion/pluginval/releases/download/v1.0.4/${asset}"

if [[ ! -x "${executable}" ]]; then
    mkdir -p "${destination}"
    curl --fail --location --retry 3 "${download_url}" --output "${archive}"
    unzip -oq "${archive}" -d "${destination}"
    chmod +x "${executable}"
fi

if [[ ! -x "${executable}" ]]; then
    echo "pluginval executable was not present in ${asset}" >&2
    exit 2
fi

printf '%s\n' "${executable}"

