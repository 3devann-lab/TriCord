#!/usr/bin/env bash
# Installs makerom and bannertool, which the Makefile needs to package TriCord.cia.
#
# Usage: scripts/install_cia_tools.sh [install-dir]   (default: /usr/local/bin)
# Tries the projects' prebuilt Linux releases first and builds from source if those are gone.
set -euo pipefail

DEST="${1:-/usr/local/bin}"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${DEST}"

api() {
	if [ -n "${GITHUB_TOKEN:-}" ]; then
		curl -fsSL --retry 3 -H "Authorization: Bearer ${GITHUB_TOKEN}" "$1"
	else
		curl -fsSL --retry 3 "$1"
	fi
}

# Prints the download URL of the newest release asset of $1 whose name matches the regex $2.
asset_url() {
	local json
	json="$(api "https://api.github.com/repos/$1/releases?per_page=20" 2>/dev/null)" || return 1
	printf '%s' "${json}" | python3 -c '
import json, re, sys
pattern = re.compile(sys.argv[1], re.I)
try:
    releases = json.load(sys.stdin)
except ValueError:
    sys.exit(1)
for release in releases:
    for asset in release.get("assets", []):
        if pattern.search(asset["name"]):
            print(asset["browser_download_url"])
            sys.exit(0)
sys.exit(1)
' "$2"
}

is_host_binary() {
	# Release archives often carry 32-bit, ARM, macOS and Windows builds next to the one we want.
	file -L "$1" 2>/dev/null | grep -qi 'ELF.*x86-64'
}

# Unpacks archive $1 and installs the Linux x86_64 executable named $2 from it into DEST.
install_from_archive() {
	local archive="$1" name="$2" dir="${WORK}/unpack-$2"
	rm -rf "${dir}"
	mkdir -p "${dir}"
	case "${archive}" in
	*.zip) unzip -q -o "${archive}" -d "${dir}" ;;
	*.tar.gz | *.tgz) tar -xzf "${archive}" -C "${dir}" ;;
	*.tar.xz) tar -xJf "${archive}" -C "${dir}" ;;
	*) cp "${archive}" "${dir}/${name}" ;;
	esac
	local candidate
	while IFS= read -r candidate; do
		if is_host_binary "${candidate}"; then
			install -m 755 "${candidate}" "${DEST}/${name}"
			echo "${name}: installed ${candidate#"${dir}"/}"
			return 0
		fi
	done < <(find "${dir}" -type f -name "${name}")
	echo "${name}: no Linux x86_64 build in ${archive##*/}" >&2
	return 1
}

install_makerom() {
	local url
	if url="$(asset_url 3DSGuy/Project_CTR 'makerom.*(ubuntu|linux).*(x86_64|x64|amd64).*\.(zip|tar\.gz)$')"; then
		echo "makerom: ${url}"
		if curl -fsSL --retry 3 -o "${WORK}/makerom-archive.${url##*.}" "${url}" &&
			install_from_archive "${WORK}/makerom-archive.${url##*.}" makerom; then
			return 0
		fi
	fi
	echo "makerom: building from source"
	git clone --depth 1 https://github.com/3DSGuy/Project_CTR.git "${WORK}/Project_CTR"
	make -C "${WORK}/Project_CTR/makerom" deps
	make -C "${WORK}/Project_CTR/makerom" -j"$(nproc)" program
	install -m 755 "$(find "${WORK}/Project_CTR/makerom" -type f -name makerom -perm -u+x | head -n1)" "${DEST}/makerom"
}

install_bannertool() {
	local url repo
	for repo in diasurgical/bannertool Steveice10/bannertool; do
		if url="$(asset_url "${repo}" '^bannertool.*\.zip$')"; then
			echo "bannertool: ${url}"
			if curl -fsSL --retry 3 -o "${WORK}/bannertool.zip" "${url}" &&
				install_from_archive "${WORK}/bannertool.zip" bannertool; then
				return 0
			fi
		fi
	done
	echo "bannertool: building from source"
	git clone --depth 1 --recursive https://github.com/diasurgical/bannertool.git "${WORK}/bannertool"
	make -C "${WORK}/bannertool" -j"$(nproc)"
	local built
	built="$(find "${WORK}/bannertool" -type f -name bannertool | while read -r f; do is_host_binary "$f" && echo "$f"; done | head -n1)"
	install -m 755 "${built}" "${DEST}/bannertool"
}

command -v makerom >/dev/null 2>&1 || install_makerom
command -v bannertool >/dev/null 2>&1 || install_bannertool
echo "makerom: $(command -v makerom)"
echo "bannertool: $(command -v bannertool)"
