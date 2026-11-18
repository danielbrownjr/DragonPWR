#!/usr/bin/env bash
# Version: 1.0
# User-owned official ESP-IDF install; no host packages, shell rc or devices.
set -euo pipefail
release=${1:?Usage: setup_idf.sh v5.3.5|v6.1.1 /absolute/path/to/python3.11}
python_bin=${2:?Supply an existing Python 3.11 executable}
case "$release" in v5.3.5|v6.1.1) ;; *) echo "Unsupported release: $release" >&2; exit 2 ;; esac
[[ "$python_bin" = /* && -x "$python_bin" ]] || { echo "Python must be an absolute executable path" >&2; exit 2; }
"$python_bin" -c 'import sys; assert sys.version_info[:2] == (3, 11), "Python 3.11 required"'
idf_root="$HOME/esp/esp-idf-$release"
export IDF_TOOLS_PATH="$HOME/.espressif-$release"
export PATH="$(dirname "$python_bin"):$PATH"
mkdir -p "$HOME/esp"
if [[ ! -e "$idf_root" ]]; then
    git clone --branch "$release" --depth 1 --shallow-submodules --recursive \
        https://github.com/espressif/esp-idf.git "$idf_root"
fi
[[ "$(git -C "$idf_root" describe --tags --exact-match)" = "$release" ]] || {
    echo "Existing checkout is not $release; refusing to switch it" >&2; exit 1;
}
[[ -z "$(git -C "$idf_root" status --porcelain --ignore-submodules)" ]] || {
    echo "Existing IDF source has edits; refusing installation" >&2; exit 1;
}
git -C "$idf_root" submodule update --init --recursive --depth 1
(cd "$idf_root" && ./install.sh esp32c2,esp32c3)
activation="$HOME/esp/activate-idf-$release.sh"
if [[ -e "$activation" ]]; then
    echo "Activation already exists; leaving it unchanged: $activation"
else
    {
        printf '# Source in a fresh Bash or Zsh shell.\n'
        printf 'export IDF_TOOLS_PATH=%q\n' "$IDF_TOOLS_PATH"
        printf 'export PATH=%q:"$PATH"\n' "$(dirname "$python_bin")"
        printf '. %q\n' "$idf_root/export.sh"
    } > "$activation"
fi
echo "Activate in a fresh shell: . $activation"
