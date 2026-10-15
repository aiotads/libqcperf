# qcperf — NPU and GPU sampler

`qcperf` samples NPU and GPU utilization on a Qualcomm Linux board (aarch64)
and prints the average over a time window as `KEY=VALUE` lines. One command
gives you both NPU and GPU numbers.

This directory is ready to use: the binary is prebuilt, nothing to compile.

## Install

Run this on your PC with the board connected. Only `adb` (or `ssh`) is needed.

```sh
./deploy.sh --adb              # board connected over USB adb
./deploy.sh --adb <serial>     # when more than one adb device is connected
./deploy.sh root@<board-ip>    # over SSH (an ssh config alias works too)
```

It copies this directory to the board, installs `qcperf` to `/usr/bin` and
`libqcperfCore.so` to `/usr/lib`, then checks NPU0, NPU1 and the GPU. You
should see:

```
==> Verifying against NPU0
==> Verifying against NPU1
==> Verifying GPU
==> Install OK: /usr/bin/qcperf
```

Already logged in on the board? Copy this directory there and run
`./install.sh` as root instead.

## Use

On the board:

```sh
qcperf <npu_index> <seconds> [sampling_rate_ms]
```

| Argument | Meaning |
|---|---|
| `npu_index` | `0` = NPU0, `1` = NPU1 |
| `seconds` | how long to sample (the result is the average), at least 1 |
| `sampling_rate_ms` | optional: `1`, `5`, `10`, `50`, `100` or `200` (default `200`) |

Example: sample NPU0 and the GPU for 3 seconds.

```console
# qcperf 0 3
NPU_INDEX=0
Q6_UTIL=26.4957
Q6_CLOCK_KHZ=1459200.0000
HVX_UTIL=6.4927
HMX_UTIL=1.1965
SAMPLES=14
GPU_UTIL=0.2667
GPU_CLOCK_MHZ=405.0000
```

| Key | Unit | Meaning |
|---|---|---|
| `NPU_INDEX` | — | which NPU was sampled |
| `Q6_UTIL` | % | NPU scalar core utilization |
| `Q6_CLOCK_KHZ` | KHz | NPU clock |
| `HVX_UTIL` | % | NPU vector unit utilization |
| `HMX_UTIL` | % | NPU matrix unit utilization |
| `SAMPLES` | count | number of NPU readings averaged |
| `GPU_UTIL` | % | GPU utilization |
| `GPU_CLOCK_MHZ` | MHz | GPU clock |

**One run samples one NPU, plus the GPU.** To read both NPUs, run it twice:

```sh
qcperf 0 3
qcperf 1 3
```

**Use the values in a shell script:**

```sh
eval "$(qcperf 0 3)"
echo "NPU0 ${Q6_UTIL}%  GPU ${GPU_UTIL}% @ ${GPU_CLOCK_MHZ} MHz"
```

The GPU numbers come from `/sys/class/kgsl/kgsl-3d0`. If the GPU cannot be
read, `qcperf` prints a warning on stderr and shows only the NPU values.

## Requirements

On the board:

- aarch64 Qualcomm Linux
- NPU: `/dev/fastrpc-cdsp` and `/dev/fastrpc-cdsp1`, and
  `/usr/lib/dsp/cdsp/libsysmonquery_skel.so` (and the `cdsp1` equivalent)
- GPU: `/sys/class/kgsl/kgsl-3d0`

## Troubleshooting

- **`GPU unavailable, reporting NPU only`**: `/sys/class/kgsl/kgsl-3d0` is not
  readable. Check that the GPU driver is loaded.
- **`connect NPU<n> failed`**: the fastrpc device node or the sysmon library
  listed under Requirements is missing.
- **`no samples collected`**: the window was too short. Use a longer
  `seconds` or a smaller `sampling_rate_ms`.
- **`qcperf: command not found`**: it was installed under a custom `PREFIX`.
  Call it by its full path.

## Uninstall

```sh
rm -f /usr/bin/qcperf /usr/lib/libqcperfCore.so
```

## Rebuilding

Only needed after changing the source. See `packaging/README.md`.
