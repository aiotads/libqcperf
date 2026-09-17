#!/usr/bin/env bash
#
# Build libqcperf's NPU sampler and lay out an install package for the target.
#
# Two supported hosts:
#   * an aarch64 Qualcomm Linux board -- builds natively, no SDK needed;
#   * an x86_64 workstation with the Qualcomm OE SDK installed -- cross-builds
#     through the SDK's OEToolchainConfig.cmake.
#
# The NPU backend only exists on Linux/aarch64, so an x86_64 build without the
# SDK produces no qcperf binary; this script refuses that case up front.
#
# Set OECORE_DIR to point at a non-default SDK install.
#
# Produces: libqcperf-npu-<version>-aarch64.tar.gz
set -euo pipefail

VERSION="${VERSION:-0.1.0.0}"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${SRC_DIR}/build-package}"
OECORE_DIR="${OECORE_DIR:-/usr/local/oecore-x86_64}"
STAGE_DIR="$(mktemp -d)"
PKG_NAME="libqcperf-npu-${VERSION}-aarch64"
OUT_TARBALL="${SRC_DIR}/${PKG_NAME}.tar.gz"

trap 'rm -rf "$STAGE_DIR"' EXIT

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED=ON
    -DBACKENDS="NPU"
    -DProjectVersion="$VERSION"
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
)

if [ "$(uname -m)" = "aarch64" ]; then
    echo "==> Configuring (native aarch64)"
else
    ENV_SETUP="$(echo "${OECORE_DIR}"/environment-setup-*-qcom-linux | head -n1)"
    if [ ! -f "$ENV_SETUP" ]; then
        echo "Error: host is $(uname -m) and no Qualcomm OE SDK was found." >&2
        echo "  Expected an environment-setup script under ${OECORE_DIR}." >&2
        echo "  Install the SDK, set OECORE_DIR, or run this script on the board." >&2
        exit 1
    fi
    echo "==> Sourcing OE SDK: ${ENV_SETUP}"
    set +u
    # shellcheck disable=SC1090
    . "$ENV_SETUP"
    set -u
    CMAKE_ARGS+=(
        -DCMAKE_TOOLCHAIN_FILE="${OECORE_NATIVE_SYSROOT}/usr/share/cmake/OEToolchainConfig.cmake"
    )
    echo "==> Configuring (cross-compiling for aarch64)"
fi

cmake -S "${SRC_DIR}/qcperf" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}" >/dev/null

echo "==> Building"
cmake --build "$BUILD_DIR" -j"$(nproc)" >/dev/null

if [ ! -x "${BUILD_DIR}/qcperf" ]; then
    echo "Error: ${BUILD_DIR}/qcperf was not built." >&2
    echo "  The NPU backend was skipped -- check the configure log above." >&2
    exit 1
fi

echo "==> Staging"
install -d "${STAGE_DIR}/${PKG_NAME}/bin" "${STAGE_DIR}/${PKG_NAME}/lib"
install -m 0755 "${BUILD_DIR}/qcperf" "${STAGE_DIR}/${PKG_NAME}/bin/"
install -m 0644 "${BUILD_DIR}/libqcperfCore.so"  "${STAGE_DIR}/${PKG_NAME}/lib/"
install -m 0755 "${SRC_DIR}/packaging/install.sh" "${STAGE_DIR}/${PKG_NAME}/"

echo "==> Packing ${OUT_TARBALL}"
tar -czf "$OUT_TARBALL" -C "$STAGE_DIR" "$PKG_NAME"
echo "$OUT_TARBALL"
