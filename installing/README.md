# qcperf — NPU and GPU sampler

`qcperf` samples NPU0, NPU1 and the GPU on a Qualcomm Linux board (aarch64)
and prints every metric, averaged over a time window, as `KEY=VALUE` lines.
One command gives you all of it; grep for what you need.

This directory is ready to use: the binary is prebuilt, nothing to compile.

## Install

Run this on your PC with the board connected. Only `adb` (or `ssh`) is needed.

```sh
./deploy.sh --adb              # board connected over USB adb
./deploy.sh --adb <serial>     # when more than one adb device is connected
./deploy.sh root@<board-ip>    # over SSH (an ssh config alias works too)
```

It copies this directory to the board, installs `qcperf` to `/usr/bin` and
`libqcperfCore.so` to `/usr/lib`, then checks that NPU0, NPU1 and the GPU all
report data. You should see:

```
==> Verifying NPU0, NPU1 and GPU
==> Install OK: /usr/bin/qcperf
```

Already logged in on the board? Copy this directory there and run
`./install.sh` as root instead.

## Use

On the board:

```sh
qcperf <seconds> [sampling_rate_ms]
```

| Argument | Meaning |
|---|---|
| `seconds` | how long to sample (the result is the average), at least 1 |
| `sampling_rate_ms` | optional: `1`, `5`, `10`, `50`, `100` or `200` (default `200`) |

NPU0, NPU1 and the GPU are sampled at the same time over the same window, so
the command takes about `seconds` to finish.

```console
# qcperf 3
NPU0_Q6_UTIL=21.7544
NPU0_Q6_CLOCK_KHZ=1459200.0000
NPU0_HVX_UTIL=4.4782
NPU0_HMX_UTIL=1.0930
NPU0_SAMPLES=14
NPU1_Q6_UTIL=0.0716
NPU1_Q6_CLOCK_KHZ=921600.0000
NPU1_HVX_UTIL=0.0000
NPU1_HMX_UTIL=0.0000
NPU1_SAMPLES=14
GPU_UTIL=0.2000
GPU_CLOCK_MHZ=405.0000
```

| Key | Unit | Meaning |
|---|---|---|
| `NPU<n>_Q6_UTIL` | % | NPU scalar core utilization |
| `NPU<n>_Q6_CLOCK_KHZ` | KHz | NPU clock |
| `NPU<n>_HVX_UTIL` | % | NPU vector unit utilization |
| `NPU<n>_HMX_UTIL` | % | NPU matrix unit utilization |
| `NPU<n>_SAMPLES` | count | number of readings averaged |
| `GPU_UTIL` | % | GPU utilization |
| `GPU_CLOCK_MHZ` | MHz | GPU clock |

**Pick what you need with grep:**

```sh
qcperf 3 | grep '^GPU'                 # GPU only
qcperf 3 | grep '^NPU1_Q6_UTIL'        # one value
```

**Use the values in a shell script:**

```sh
eval "$(qcperf 3)"
echo "NPU0 ${NPU0_Q6_UTIL}%  NPU1 ${NPU1_Q6_UTIL}%  GPU ${GPU_UTIL}%"
```

The GPU numbers come from `/sys/class/kgsl/kgsl-3d0`. If the GPU cannot be
read, `qcperf` prints a warning on stderr and leaves the `GPU_*` lines out. If
an NPU cannot be read, its lines are left out and the exit code is non-zero.

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
- **`no samples collected for NPU<n>`**: the window was too short. Use a longer
  `seconds` or a smaller `sampling_rate_ms`.
- **`Usage: ...`**: the old form `qcperf <npu_index> <seconds>` is gone. Run
  `qcperf <seconds>`.
- **`qcperf: command not found`**: it was installed under a custom `PREFIX`.
  Call it by its full path.

## Uninstall

```sh
rm -f /usr/bin/qcperf /usr/lib/libqcperfCore.so
```

## Rebuilding

Only needed after changing the source. See `packaging/README.md`.
