#!/usr/bin/env bash
#
# Install the libqcperf NPU and GPU samplers.
#
# Default prefix is /usr, so the binary lands in /usr/bin and is on the default
# PATH with no shell setup. The binary carries an RPATH of $ORIGIN/../lib, so
# it resolves /usr/lib/libqcperfCore.so and needs no LD_LIBRARY_PATH; that
# holds for any other prefix with the same bin/lib layout.
set -euo pipefail

PREFIX="${PREFIX:-/usr}"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: must run as root" >&2
    exit 1
fi

# Some ostree images mount /usr read-only. Remount it writable for the install
# and put it back the way it was on the way out, so the script leaves no
# lingering change to the system's mount state.
REMOUNTED_RO_TARGET=""
restore_mount() {
    if [ -n "$REMOUNTED_RO_TARGET" ]; then
        echo "==> Restoring ${REMOUNTED_RO_TARGET} to read-only"
        mount -o remount,ro "$REMOUNTED_RO_TARGET" || true
    fi
}
trap restore_mount EXIT

MOUNT_TARGET="$(findmnt -no TARGET --target "$PREFIX" 2>/dev/null || true)"
if [ -n "$MOUNT_TARGET" ] &&
   findmnt -no OPTIONS --target "$PREFIX" 2>/dev/null | grep -qE '(^|,)ro(,|$)'; then
    echo "==> ${MOUNT_TARGET} is read-only, remounting read-write"
    mount -o remount,rw "$MOUNT_TARGET"
    REMOUNTED_RO_TARGET="$MOUNT_TARGET"
fi

echo "==> Installing to ${PREFIX}"
install -d "${PREFIX}/bin" "${PREFIX}/lib"
install -m 0755 "${SRC_DIR}/bin/qcperf" "${PREFIX}/bin/"
install -m 0644 "${SRC_DIR}/lib/libqcperfCore.so"  "${PREFIX}/lib/"

echo "==> Verifying against NPU0"
if ! "${PREFIX}/bin/qcperf" 0 2 >/dev/null; then
    echo "Error: sampler failed on NPU0. Check that" >&2
    echo "  /usr/lib/dsp/cdsp/libsysmonquery_skel.so and /dev/fastrpc-cdsp exist." >&2
    exit 1
fi
echo "==> Verifying against NPU1"
if ! "${PREFIX}/bin/qcperf" 1 2 >/dev/null; then
    echo "Error: sampler failed on NPU1. Check /usr/lib/dsp/cdsp1/ and /dev/fastrpc-cdsp1." >&2
    exit 1
fi

echo "==> Verifying GPU"
if ! "${PREFIX}/bin/qcperf" 0 2 2>/dev/null | grep -q '^GPU_UTIL='; then
    echo "Error: qcperf reported no GPU metrics. Check that /sys/class/kgsl/kgsl-3d0 exists." >&2
    exit 1
fi

echo "==> Install OK: ${PREFIX}/bin/qcperf"
