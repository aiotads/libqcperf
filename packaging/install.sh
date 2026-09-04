#!/usr/bin/env bash
#
# Install the libqcperf NPU sampler.
#
# Default prefix is /opt/qcom/libqcperf, matching where QualcommProfiler lives.
# /usr is read-only on these ostree images, so nothing is written there; the
# binary carries an RPATH of $ORIGIN/../lib and needs no LD_LIBRARY_PATH.
set -euo pipefail

PREFIX="${PREFIX:-/opt/qcom/libqcperf}"
LINK_DIR="${LINK_DIR:-/usr/local/bin}"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: must run as root" >&2
    exit 1
fi

echo "==> Installing to ${PREFIX}"
install -d "${PREFIX}/bin" "${PREFIX}/lib"
install -m 0755 "${SRC_DIR}/bin/qcperf-npu-sample" "${PREFIX}/bin/"
install -m 0644 "${SRC_DIR}/lib/libqcperfCore.so"  "${PREFIX}/lib/"

if [ -d "$LINK_DIR" ] && [ -w "$LINK_DIR" ]; then
    ln -sf "${PREFIX}/bin/qcperf-npu-sample" "${LINK_DIR}/qcperf-npu-sample"
    echo "==> Linked ${LINK_DIR}/qcperf-npu-sample"
else
    echo "==> ${LINK_DIR} not writable, skipping symlink"
fi

echo "==> Verifying against NPU0"
if ! "${PREFIX}/bin/qcperf-npu-sample" 0 2 >/dev/null; then
    echo "Error: sampler failed on NPU0. Check that" >&2
    echo "  /usr/lib/dsp/cdsp/libsysmonquery_skel.so and /dev/fastrpc-cdsp exist." >&2
    exit 1
fi
echo "==> Verifying against NPU1"
if ! "${PREFIX}/bin/qcperf-npu-sample" 1 2 >/dev/null; then
    echo "Error: sampler failed on NPU1. Check /usr/lib/dsp/cdsp1/ and /dev/fastrpc-cdsp1." >&2
    exit 1
fi

echo "==> Install OK: $(command -v qcperf-npu-sample || echo "${PREFIX}/bin/qcperf-npu-sample")"
