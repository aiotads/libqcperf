#!/usr/bin/env bash
#
# Build libqcperf's NPU sampler on the target and lay out an install package.
#
# Run this on an aarch64 Qualcomm Linux board (it builds natively -- do NOT pass
# TARGET_ARCH, that forces the cross toolchain file and fails without the ARM
# GNU toolchain installed).
#
# Produces: libqcperf-npu-<version>-aarch64.tar.gz
set -euo pipefail

VERSION="${VERSION:-0.1.0.0}"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SRC_DIR}/build-npu"
STAGE_DIR="$(mktemp -d)"
PKG_NAME="libqcperf-npu-${VERSION}-aarch64"
OUT_TARBALL="${SRC_DIR}/${PKG_NAME}.tar.gz"

trap 'rm -rf "$STAGE_DIR"' EXIT

echo "==> Configuring (native aarch64)"
cmake -S "${SRC_DIR}/qcperf" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED=ON \
    -DBACKENDS="NPU" \
    -DProjectVersion="$VERSION" >/dev/null

echo "==> Building"
cmake --build "$BUILD_DIR" -j"$(nproc)" >/dev/null

echo "==> Staging"
install -d "${STAGE_DIR}/${PKG_NAME}/bin" "${STAGE_DIR}/${PKG_NAME}/lib"
install -m 0755 "${BUILD_DIR}/qcperf-npu-sample" "${STAGE_DIR}/${PKG_NAME}/bin/"
install -m 0644 "${BUILD_DIR}/libqcperfCore.so"  "${STAGE_DIR}/${PKG_NAME}/lib/"
install -m 0755 "${SRC_DIR}/packaging/install.sh" "${STAGE_DIR}/${PKG_NAME}/"

echo "==> Packing ${OUT_TARBALL}"
tar -czf "$OUT_TARBALL" -C "$STAGE_DIR" "$PKG_NAME"
echo "$OUT_TARBALL"
