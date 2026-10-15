# qcperf — build, install and usage

`qcperf` is a one-shot NSP and GPU utilization sampler built on libqcperf. It
averages the DSP NPU metrics, and the GPU metrics, over a fixed window and
prints them as `KEY=VALUE` lines, so shell monitors can consume it directly.

Two directories at the repository root:

| Directory | Purpose |
|---|---|
| `installing/` | ready-to-use package: prebuilt `qcperf`, `install.sh`, `deploy.sh`, and a short README. Use this to put `qcperf` on a board. |
| `packaging/` | `make-package.sh`, which rebuilds the binaries and refreshes `installing/`. Use this after changing the source. |

If you only want to install `qcperf`, you do not need this directory; follow
`installing/README.md`.

## Rebuild after changing the source

```bash
bash packaging/make-package.sh
```

This builds the NPU and GPU backends and overwrites `installing/bin/qcperf`
and `installing/lib/libqcperfCore.so`. Commit the refreshed files together with
the source change, so `installing/` always matches the code.

**Build host.** Either the board itself, or an x86_64 workstation with the
Qualcomm OE SDK installed. `make-package.sh` detects which case it is:

- on aarch64 it builds natively, no SDK needed;
- on x86_64 it sources the SDK environment script and cross-compiles.

The SDK is expected at `/usr/local/oecore-x86_64`. Point `OECORE_DIR` at a
different install if yours lives elsewhere.

The NPU backend only exists on Linux/aarch64. An x86_64 build without the SDK
produces no binary at all, so the script refuses that case up front.

Environment overrides:

| Variable | Default | Meaning |
|---|---|---|
| `VERSION` | `0.1.0.0` | version string baked into the binary |
| `BUILD_DIR` | `<repo>/build-package` | CMake build tree |
| `OECORE_DIR` | `/usr/local/oecore-x86_64` | Qualcomm OE SDK install root |

## Install

```bash
cd installing
./deploy.sh --adb [serial]        # over USB adb
./deploy.sh root@<board-ip>       # over SSH (an ssh config alias works too)
```

`deploy.sh` copies `installing/` to the board and runs `install.sh` there. On
a board you are already logged into, run `./install.sh` as root instead.

The installer puts `qcperf` in `/usr/bin` and `libqcperfCore.so` in `/usr/lib`,
then samples NPU0, NPU1 and the GPU once each and fails loudly if any does not
respond.

```
==> Installing to /usr
==> Verifying against NPU0
==> Verifying against NPU1
==> Verifying GPU
==> Install OK: /usr/bin/qcperf
```

`/usr/bin` is on the default PATH, so `qcperf` is callable by name with no
shell setup. The binary carries an RPATH of `$ORIGIN/../lib`, so it resolves
`/usr/lib/libqcperfCore.so` with no `LD_LIBRARY_PATH` set.

Some ostree images mount `/usr` read-only. The installer remounts it
read-write for the duration of the install and tries to restore it on the way
out. If the restore fails with `mount point is busy`, `/usr` stays writable
until the next reboot. On an ostree system a later OS update replaces the
`/usr` tree, which removes the sampler; reinstall after an update.

Set `PREFIX` to install somewhere else. Any prefix with the same `bin`/`lib`
layout works, though outside `/usr` you have to call the binary by its full
path or extend PATH yourself.

```bash
PREFIX=/opt/innodisk/qcperf ./deploy.sh --adb
```

To remove the install:

```bash
rm -f /usr/bin/qcperf /usr/lib/libqcperfCore.so
```

## Usage

```
qcperf <npu_index> <profile_seconds> [sampling_rate_ms]
```

| Argument | Values | Meaning |
|---|---|---|
| `npu_index` | `0` or `1` | `0` is NPU0 (cdsp), `1` is NPU1 (cdsp1) |
| `profile_seconds` | integer, at least 1 | averaging window |
| `sampling_rate_ms` | `1`, `5`, `10`, `50`, `100` or `200` | sampling period, default `200` |

Sample NPU0 and the GPU for three seconds:

```console
# qcperf 0 3
NPU_INDEX=0
Q6_UTIL=35.0678
Q6_CLOCK_KHZ=921600.0000
HVX_UTIL=26.1441
HMX_UTIL=5.9012
SAMPLES=14
GPU_UTIL=0.2667
GPU_CLOCK_MHZ=405.0000
```

Output fields:

| Key | Unit | Meaning |
|---|---|---|
| `NPU_INDEX` | — | which NSP was sampled |
| `Q6_UTIL` | % | Hexagon Q6 scalar core utilization |
| `Q6_CLOCK_KHZ` | KHz | Q6 clock frequency |
| `HVX_UTIL` | % | vector unit utilization |
| `HMX_UTIL` | % | matrix unit utilization |
| `SAMPLES` | count | NPU readings averaged, after the warm-up reading is dropped |
| `GPU_UTIL` | % | Adreno GPU busy percentage |
| `GPU_CLOCK_MHZ` | MHz | current GPU clock |

The GPU values are read from `/sys/class/kgsl/kgsl-3d0` (`gpu_busy_percentage`
and `gpuclk`) over the same window. If the GPU cannot be read, a warning goes
to stderr and only the NPU keys are printed. GPU readings are the kgsl
driver's own busy statistic, so they track but do not match Qualcomm Profiler
(qprof) sample for sample.

The first NPU reading after the sysmon session opens measures the session setup
rather than the workload, so it is discarded. A window must therefore be long
enough to produce at least two readings; with the default 200 ms rate, one
second is already enough.

The backend holds a single sysmon session per process, so one process monitors
one NSP. To watch both NPUs, run the tool twice:

```bash
for npu in 0 1; do
    qcperf "$npu" 5
done
```

Because the output is plain `KEY=VALUE`, a shell can consume it with no parser:

```bash
eval "$(qcperf 0 5)"
echo "NPU0 Q6: ${Q6_UTIL}%  GPU: ${GPU_UTIL}%"
```

The tool exits zero only when it collected at least one usable NPU sample.
Diagnostics go to stderr prefixed with `[qcperf]`, keeping stdout clean for
the `KEY=VALUE` block.

## Troubleshooting

**`qcperf: command not found` after a successful install.** The default prefix
puts the binary on PATH, so this means it was installed under a custom
`PREFIX`. Call it by its full path, or add that prefix's `bin` to PATH.

**`connect NPU<n> failed`.** The sysmon skeleton library or the fastrpc device
node is missing. Check `/usr/lib/dsp/cdsp/libsysmonquery_skel.so` and
`/dev/fastrpc-cdsp` for NPU0, and the `cdsp1` equivalents for NPU1.

**`GPU unavailable, reporting NPU only`.** `/sys/class/kgsl/kgsl-3d0` cannot be
read. Check that the Adreno kgsl driver is loaded. The same message appears if
a stale `/usr/lib/libqcperfCore.so` without the GPU backend is picked up
instead of the one next to the binary.

**`start failed` mentioning the sampling rate.** `sampling_rate_ms` accepts
only 1, 5, 10, 50, 100 and 200.

**`no samples collected`.** The window closed before a second reading arrived.
Use a longer window or a faster sampling rate.

**`make-package.sh` fails with `qcperf was not built`.** The NPU backend was
skipped at configure time, which on an x86_64 host means the OE SDK was not
found. Check the configure log above the error and confirm `OECORE_DIR`.

**Prerequisites on the board.** Qualcomm Linux on aarch64 with
`/dev/fastrpc-cdsp`, `/dev/fastrpc-cdsp1`,
`/usr/lib/dsp/cdsp/libsysmonquery_skel.so` (and the `cdsp1` equivalent), and
`/sys/class/kgsl/kgsl-3d0` for the GPU.
