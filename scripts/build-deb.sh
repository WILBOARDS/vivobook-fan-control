#!/usr/bin/env bash
# Builds two .deb packages without debhelper: a headless command line package
# and the graphical one that depends on it. Only needs the build toolchain.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

arch="$(dpkg --print-architecture)"
out="$root/debian/out"

version="$(sed -n 's/^project([[:space:]]*[a-z-]*[[:space:]]*VERSION[[:space:]]\+\([0-9.]*\).*/\1/p' CMakeLists.txt)"
if [[ -z "$version" ]]; then
    echo "could not read the version from CMakeLists.txt" >&2
    exit 1
fi

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBEXECDIR=libexec >/dev/null
cmake --build build >/dev/null

# package  stage dir      script dir  control stanza
finish() {
    local pkg="$1" stage="$2" scripts="$3"
    local deb="$out/${pkg}_${version}_${arch}.deb"

    rm -rf "$stage"
    mkdir -p "$stage/DEBIAN"
    DESTDIR="$stage" cmake --install build --component "$scripts" >/dev/null

    local binaries=()
    while IFS= read -r file; do binaries+=("$file"); done < <(find "$stage" -type f -perm -u+x ! -path "$stage/DEBIAN/*")
    if ((${#binaries[@]} == 0)); then
        echo "no binaries staged for $pkg" >&2
        exit 1
    fi

    local substvars="$stage/DEBIAN/substvars"
    dpkg-shlibdeps -T"$substvars" "${binaries[@]}" >/dev/null
    local shlibs
    shlibs="$(sed -n 's/^shlibs:Depends=//p' "$substvars")"
    if [[ -z "$shlibs" ]]; then
        echo "dpkg-shlibdeps produced no dependency list for $pkg" >&2
        exit 1
    fi
    rm -f "$substvars"

    local size
    size="$(du -sk "$stage" | cut -f1)"

    # Only the matching binary stanza belongs in DEBIAN/control.
    awk -v pkg="$pkg" '/^Package:/{keep=($2==pkg)} keep' debian/control |
        sed -e "s|@VERSION@|$version|g" \
            -e "s|@ARCH@|$arch|g" \
            -e "s|@SIZE@|$size|g" \
            -e "s|@SHLIBS@|$shlibs|g" \
            >"$stage/DEBIAN/control"
    grep -q '^Package:' "$stage/DEBIAN/control" || { echo "no control stanza for $pkg" >&2; exit 1; }

    if [[ -d "debian/$scripts" ]]; then
        for script in postinst prerm postrm; do
            if [[ -f "debian/$scripts/$script" ]]; then
                install -m 0755 "debian/$scripts/$script" "$stage/DEBIAN/$script"
            fi
        done
    fi

    ( cd "$stage" && find . -type f ! -path './DEBIAN/*' -printf '%P\n' | sort |
        while read -r file; do
            printf '%s  %s\n' "$(md5sum "$file" | cut -d' ' -f1)" "$file"
        done ) >"$stage/DEBIAN/md5sums"

    dpkg-deb --root-owner-group -Zzstd -b "$stage" "$deb" >/dev/null
    echo "built $deb"
}

mkdir -p "$out"
rm -f "$out"/*.deb

finish vivobook-fan-control-cli debian/stage-cli cli
finish vivobook-fan-control debian/stage-gui gui

echo
for deb in "$out"/*.deb; do
    echo "--- $(basename "$deb")"
    dpkg-deb -f "$deb" Package Version Depends | sed 's/^/    /'
    dpkg-deb -c "$deb" | awk '$6 !~ /\/$/ {print "      " $6}'
done
