#!/bin/bash
set -e

derive_version() {
    local desc
    desc="$(git describe --tags --long --always --dirty 2>/dev/null || echo "0.0.0-0-gunknown")"

    # Convert common tag format to Debian-compatible version:
    # v1.2.3-4-gabc1234 -> 1.2.3+4.gabc1234
    if [[ "$desc" =~ ^v?([0-9]+\.[0-9]+\.[0-9]+)-([0-9]+)-g([0-9a-fA-F]+)(-dirty)?$ ]]; then
        local base="${BASH_REMATCH[1]}"
        local count="${BASH_REMATCH[2]}"
        local hash="${BASH_REMATCH[3]}"
        local dirty="${BASH_REMATCH[4]}"
        if [[ "$count" == "0" ]]; then
            echo "${base}${dirty:+~dirty}"
        else
            echo "${base}+${count}.g${hash}${dirty:+~dirty}"
        fi
        return
    fi

    # Fallback for hash-only descriptions.
    if [[ "$desc" =~ ^([0-9a-fA-F]+)(-dirty)?$ ]]; then
        local hash="${BASH_REMATCH[1]}"
        local dirty="${BASH_REMATCH[2]}"
        echo "0.0.0+git.${hash}${dirty:+~dirty}"
        return
    fi

    # Last resort: keep only Debian-safe characters.
    echo "$desc" | sed -E 's/^v//; s/[^0-9A-Za-z.+:~]+/./g'
}

VERSION="${VERSION:-$(derive_version)}"
COMMIT_HASH="$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
GIT_DESCRIBE="$(git describe --tags --long --always --dirty 2>/dev/null || echo unknown)"
BUILD_DIR="build_deb"
PKG_DIR="${BUILD_DIR}/hailstoned_${VERSION}_amd64"

echo "Building binaries..."
mkdir -p build && cd build
cmake ..
make -j$(nproc)
cd ..

echo "Preparing package structure..."
rm -rf ${BUILD_DIR}
mkdir -p ${PKG_DIR}/DEBIAN
mkdir -p ${PKG_DIR}/usr/bin
mkdir -p ${PKG_DIR}/etc/xinetd.d
mkdir -p ${PKG_DIR}/usr/share/doc/hailstoned

cp build/hailstoned ${PKG_DIR}/usr/bin/
cp build/hailstone_cpu ${PKG_DIR}/usr/bin/
cp build/hailstone_cpu_nosteps ${PKG_DIR}/usr/bin/
# Optional binaries
[ -f build/hailstone_vulkan ] && cp build/hailstone_vulkan ${PKG_DIR}/usr/bin/
[ -f build/hailstone_vulkan_nosteps ] && cp build/hailstone_vulkan_nosteps ${PKG_DIR}/usr/bin/
[ -f build/hailstone_hip ] && cp build/hailstone_hip ${PKG_DIR}/usr/bin/
[ -f build/hailstone_hip_nosteps ] && cp build/hailstone_hip_nosteps ${PKG_DIR}/usr/bin/

mkdir -p ${PKG_DIR}/usr/share/hailstone
cp build/allowed_suffixes_*.bin ${PKG_DIR}/usr/share/hailstone/ 2>/dev/null || true
cp build/*.spv ${PKG_DIR}/usr/share/hailstone/ 2>/dev/null || true

cp distributed/hailstoned.xinetd ${PKG_DIR}/etc/xinetd.d/hailstoned

cat << EOF > ${PKG_DIR}/usr/share/doc/hailstoned/build-info
Build commit: ${COMMIT_HASH}
Git describe: ${GIT_DESCRIBE}
Build timestamp (UTC): $(date -u +"%Y-%m-%dT%H:%M:%SZ")
EOF

cat << EOF > ${PKG_DIR}/DEBIAN/control
Package: hailstoned
Version: ${VERSION}
Architecture: amd64
Maintainer: mev <mev@example.com>
Depends: xinetd, libc6
Description: Hailstone Distributed Search Daemon
 A high-performance daemon wrapper for hailstone compute cluster workers.
 Built from commit: ${COMMIT_HASH}
EOF

cat << 'EOF' > ${PKG_DIR}/DEBIAN/postinst
#!/bin/sh
if command -v systemctl >/dev/null 2>&1; then
    systemctl reload xinetd || systemctl restart xinetd
else
    service xinetd reload || service xinetd restart
fi
rm -f /tmp/hailstoned_benchmarks.cache
exit 0
EOF
chmod +x ${PKG_DIR}/DEBIAN/postinst

echo "Building Debian package..."
dpkg-deb --build ${PKG_DIR}
mv ${BUILD_DIR}/hailstoned_${VERSION}_amd64.deb ./
echo "Done! Package generated at ./hailstoned_${VERSION}_amd64.deb"
echo "Source commit: ${COMMIT_HASH} (${GIT_DESCRIBE})"
