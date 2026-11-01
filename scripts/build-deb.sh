#!/usr/bin/env bash
# Builds the .deb without debhelper: stage the cmake install tree, then let
# dpkg-deb pack it. Only needs the toolchain the app is built with.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

arch="$(dpkg --print-architecture)"
stage="$root/debian/stage"
out="$root/debian/out"

version="$(sed -n 's/^project([[:space:]]*[a-z-]*[[:space:]]*VERSION[[:space:]]\+\([0-9.]*\).*/\1/p' CMakeLists.txt)"
if [[ -z "$version" ]]; then
    echo "could not read the version from CMakeLists.txt" >&2
    exit 1
fi

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBEXECDIR=libexec >/dev/null
cmake --build build >/dev/null

rm -rf "$stage"
mkdir -p "$stage"
DESTDIR="$stage" cmake --install build >/dev/null

# Real shared library dependencies, resolved from the staged binaries.
shlibvars="$root/debian/substvars"
rm -f "$shlibvars"
dpkg-shlibdeps -T"$shlibvars" \
    "$stage/usr/bin/vivobook-fan-control" \
    "$stage/usr/libexec/vivobook-fan-control/fancontrol-helper" >/dev/null
shlibs="$(sed -n 's/^shlibs:Depends=//p' "$shlibvars")"
if [[ -z "$shlibs" ]]; then
    echo "dpkg-shlibdeps produced no dependency list" >&2
    exit 1
fi

size="$(du -sk --exclude=DEBIAN "$stage" | cut -f1)"

mkdir -p "$stage/DEBIAN"
# Only the binary stanza belongs in DEBIAN/control.
awk '/^$/{blank=1; next} /^Package:/{found=1} found{if(blank)printf "\n"; blank=0; print}' debian/control |
    sed -e "s|@VERSION@|$version|g" \
        -e "s|@ARCH@|$arch|g" \
        -e "s|@SIZE@|$size|g" \
        -e "s|@SHLIBS@|$shlibs|g" \
        >"$stage/DEBIAN/control"

for script in postinst prerm postrm; do
    install -m 0755 "debian/$script" "$stage/DEBIAN/$script"
done

find "$stage" -type f ! -path "$stage/DEBIAN/*" -printf '%P\n' | sort |
    while read -r file; do
        printf '%s  %s\n' "$(md5sum "$stage/$file" | cut -d' ' -f1)" "$file"
    done >"$stage/DEBIAN/md5sums"

mkdir -p "$out"
rm -f "$out"/*.deb
deb="$out/vivobook-fan-control_${version}_${arch}.deb"
dpkg-deb --root-owner-group -Zzstd -b "$stage" "$deb" >/dev/null

echo "built $deb"
dpkg-deb -I "$deb" | sed -n '2,25p'
