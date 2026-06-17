#!/bin/bash
set -e

VERSION="1.0.0"
BUILD_DIR="build_deb"
PKG_DIR="${BUILD_DIR}/hailstoned_${VERSION}_amd64"

echo "Building binaries..."
mkdir -p build && cd build
cmake ..
make -j$(nproc) hailstoned hailstone_cpu
cd ..

echo "Preparing package structure..."
rm -rf ${BUILD_DIR}
mkdir -p ${PKG_DIR}/DEBIAN
mkdir -p ${PKG_DIR}/usr/bin
mkdir -p ${PKG_DIR}/etc/xinetd.d

cp build/hailstoned ${PKG_DIR}/usr/bin/
cp build/hailstone_cpu ${PKG_DIR}/usr/bin/
# Optional binaries
[ -f build/hailstone_vulkan ] && cp build/hailstone_vulkan ${PKG_DIR}/usr/bin/
[ -f build/hailstone_hip ] && cp build/hailstone_hip ${PKG_DIR}/usr/bin/

cp distributed/hailstoned.xinetd ${PKG_DIR}/etc/xinetd.d/hailstoned

cat << 'EOF' > ${PKG_DIR}/DEBIAN/control
Package: hailstoned
Version: 1.0.0
Architecture: amd64
Maintainer: mev <mev@example.com>
Depends: xinetd, libc6
Description: Hailstone Distributed Search Daemon
 A high-performance daemon wrapper for hailstone compute cluster workers.
EOF

cat << 'EOF' > ${PKG_DIR}/DEBIAN/postinst
#!/bin/sh
if command -v systemctl >/dev/null 2>&1; then
    systemctl reload xinetd || systemctl restart xinetd
else
    service xinetd reload || service xinetd restart
fi
exit 0
EOF
chmod +x ${PKG_DIR}/DEBIAN/postinst

echo "Building Debian package..."
dpkg-deb --build ${PKG_DIR}
mv ${BUILD_DIR}/hailstoned_${VERSION}_amd64.deb ./
echo "Done! Package generated at ./hailstoned_${VERSION}_amd64.deb"
