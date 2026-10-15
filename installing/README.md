# qcperf NPU and GPU sampler

One-shot NSP and GPU utilization sampler for Qualcomm Linux (aarch64). It
averages the metrics over a fixed window and prints them as `KEY=VALUE` lines.

## Install

Run from a host that can reach the board; nothing else needs to be installed.

```sh
./deploy.sh root@192.168.0.152     # over SSH (an ssh config alias works too)
./deploy.sh --adb [serial]         # over USB adb
```

This copies the package to the board, installs `qcperf` to `/usr/bin` and
`libqcperfCore.so` to `/usr/lib`, and checks that both NPUs and
the GPU respond.
Set `PREFIX=/some/dir` to install elsewhere.

Already logged in on the board? Run `./install.sh` there as root instead.

## Use

```sh
qcperf <npu_index> <profile_seconds> [sampling_rate_ms]
qcperf 0 3      # sample NPU0 for 3 seconds
```

Output: `NPU_INDEX`, `Q6_UTIL` (%), `Q6_CLOCK_KHZ`, `HVX_UTIL` (%),
`HMX_UTIL` (%), `SAMPLES`, then `GPU_UTIL` (%) and `GPU_CLOCK_MHZ` from the same
window. If the GPU cannot be read, a warning goes to stderr and only the NPU
keys are printed. `sampling_rate_ms` is one of 1, 5, 10, 50, 100, 200
(default 200).

The GPU values come from `/sys/class/kgsl/kgsl-3d0`.

## Requirements

- Board: aarch64, `/dev/fastrpc-cdsp` and `/dev/fastrpc-cdsp1`,
  `/usr/lib/dsp/cdsp/libsysmonquery_skel.so` (and the `cdsp1` equivalent).
- GPU: `/sys/class/kgsl/kgsl-3d0` (Adreno kgsl driver).
- Host: `ssh` and `tar` (SSH mode), or `adb` (adb mode).

## Uninstall

```sh
rm -f /usr/bin/qcperf /usr/lib/libqcperfCore.so
```
