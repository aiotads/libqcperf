#!/usr/bin/env bash
#
# Deploy the libqcperf NPU sampler from this host onto a target board.
#
# Pushes the unpacked package to the board and runs its install.sh there,
# over SSH or USB adb. Only this extracted package is needed on the host.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REMOTE_DIR="/tmp/$(basename "$SCRIPT_DIR")"

MODE=""
SSH_TARGET=""
ADB_SERIAL=""

usage() {
    cat <<'USAGE'
Usage:
  ./deploy.sh <ssh-target>     Deploy over SSH
                               e.g. ./deploy.sh root@192.168.0.152, ./deploy.sh test
  ./deploy.sh --adb [serial]   Deploy over USB adb
                               serial is optional if only one device
  ./deploy.sh --help           Show this help

Set PREFIX to install somewhere other than /usr, e.g.
  PREFIX=/opt/qcperf ./deploy.sh test
USAGE
}

info() {
    printf '[info] %s\n' "$*"
}

die() {
    printf '[error] %s\n' "$*" >&2
    exit 1
}

parseArgs() {
    if [ "$#" -eq 0 ]; then
        usage >&2
        die "missing target argument"
    fi
    case "$1" in
        --help|-h)
            usage
            exit 0
            ;;
        --adb)
            MODE="adb"
            ADB_SERIAL="${2:-}"
            ;;
        -*)
            usage >&2
            die "unknown option: $1"
            ;;
        *)
            MODE="ssh"
            SSH_TARGET="$1"
            ;;
    esac
}

checkPackage() {
    [ -x "${SCRIPT_DIR}/bin/qcperf" ] && [ -f "${SCRIPT_DIR}/lib/libqcperfCore.so" ] &&
        [ -x "${SCRIPT_DIR}/install.sh" ] || die \
        "the package is incomplete (bin/qcperf, lib/libqcperfCore.so or install.sh missing) — extract the tar.gz again"
}

requireTool() {
    command -v "$1" >/dev/null 2>&1 || die "command not found: $1 — $2"
}

resolveAdbSerial() {
    local available count
    available="$(adb devices | awk 'NR > 1 && $2 == "device" {print $1}')"
    count="$(printf '%s' "${available}" | grep -c . || true)"
    if [ -n "${ADB_SERIAL}" ]; then
        printf '%s\n' "${available}" | grep -qxF "${ADB_SERIAL}" || die \
            "no adb device in state 'device' with serial ${ADB_SERIAL}; currently available: $(printf '%s' "${available}" | tr '\n' ' ')"
        return
    fi
    [ "${count}" -gt 0 ] || die \
        "no adb device detected — check the USB connection and authorization, or use ./deploy.sh <ssh-target> instead"
    [ "${count}" -eq 1 ] || die \
        "multiple adb devices detected, please pass a serial: $(printf '%s' "${available}" | tr '\n' ' ')"
    ADB_SERIAL="${available}"
}

preFlight() {
    checkPackage
    if [ "${MODE}" = "adb" ]; then
        requireTool adb "install android-tools-adb, or use SSH mode"
        resolveAdbSerial
        adb -s "${ADB_SERIAL}" shell 'echo ok' >/dev/null 2>&1 || die \
            "adb shell is not responding (serial ${ADB_SERIAL})"
        info "adb target: ${ADB_SERIAL}"
    else
        requireTool ssh "install openssh-client"
        requireTool tar "install tar"
        ssh -o ConnectTimeout=8 -o BatchMode=yes "${SSH_TARGET}" 'echo ok' >/dev/null 2>&1 || die \
            "cannot reach ${SSH_TARGET} over SSH — check the network, IP and ssh key, or use ./deploy.sh --adb instead"
        info "SSH target: ${SSH_TARGET}"
    fi
}

pushPackage() {
    info "pushing package to ${REMOTE_DIR}"
    if [ "${MODE}" = "adb" ]; then
        adb -s "${ADB_SERIAL}" shell "rm -rf ${REMOTE_DIR} && mkdir -p ${REMOTE_DIR}"
        adb -s "${ADB_SERIAL}" push "${SCRIPT_DIR}/." "${REMOTE_DIR}" >/dev/null
    else
        tar -C "${SCRIPT_DIR}" -cf - . |
            ssh "${SSH_TARGET}" "rm -rf ${REMOTE_DIR} && mkdir -p ${REMOTE_DIR} && tar -C ${REMOTE_DIR} -xf -"
    fi
}

# The board runs install.sh as root, so the script is invoked as-is; PREFIX is
# forwarded only when set.
runInstall() {
    local cmd="cd ${REMOTE_DIR} && PREFIX=${PREFIX:-/usr} ./install.sh"
    info "running install.sh on the board"
    if [ "${MODE}" = "adb" ]; then
        adb -s "${ADB_SERIAL}" shell "${cmd}"
    else
        ssh "${SSH_TARGET}" "${cmd}"
    fi
}

cleanup() {
    if [ "${MODE}" = "adb" ]; then
        adb -s "${ADB_SERIAL}" shell "rm -rf ${REMOTE_DIR}" || true
    else
        ssh "${SSH_TARGET}" "rm -rf ${REMOTE_DIR}" || true
    fi
}

main() {
    parseArgs "$@"
    preFlight
    pushPackage
    trap cleanup EXIT
    runInstall
    info "deploy complete"
}

main "$@"
