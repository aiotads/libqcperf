# qcperf NPU sampler — packaging, install and usage

`qcperf` is a one-shot NSP utilization sampler built on libqcperf. It averages
the DSP NPU metrics over a fixed window and prints them as `KEY=VALUE` lines,
so shell monitors can consume it directly.

This directory holds the two scripts that ship it to a board:

| Script | Runs on | Purpose |
|---|---|---|
| `make-package.sh` | build host | builds the sampler and produces a tarball |
| `install.sh` | target board | installs the tarball's contents and verifies both NPUs |

## Prerequisites

**Target board.** Qualcomm Linux on aarch64, with the NPU sysmon plumbing
present:

- `/dev/fastrpc-cdsp` and `/dev/fastrpc-cdsp1`
- `/usr/lib/dsp/cdsp/libsysmonquery_skel.so` and the `cdsp1` equivalent

**Build host.** Either the board itself, or an x86_64 workstation with the
Qualcomm OE SDK installed. `make-package.sh` detects which case it is:

- on aarch64 it builds natively, no SDK needed;
- on x86_64 it sources the SDK environment script and cross-compiles.

The SDK is expected at `/usr/local/oecore-x86_64`. Point `OECORE_DIR` at a
different install if yours lives elsewhere.

The NPU backend only exists on Linux/aarch64. An x86_64 build without the SDK
produces no binary at all, so the script refuses that case up front rather
than packaging an empty tarball.

## Build the package

```bash
bash packaging/make-package.sh
```

This writes `libqcperf-npu-<version>-aarch64.tar.gz` at the repository root
and prints its path. The tarball contains `bin/qcperf`, `lib/libqcperfCore.so`
and a copy of `install.sh`.

Environment overrides:

| Variable | Default | Meaning |
|---|---|---|
| `VERSION` | `0.1.0.0` | version string baked into the binary and the tarball name |
| `BUILD_DIR` | `<repo>/build-package` | CMake build tree |
| `OECORE_DIR` | `/usr/local/oecore-x86_64` | Qualcomm OE SDK install root |

## Deploy and install

Copy the tarball to the board, unpack it and run the installer as root. `<board>`
below is an SSH host alias or an IP address.

```bash
scp libqcperf-npu-0.1.0.0-aarch64.tar.gz <board>:/tmp/
ssh <board>
tar -xzf /tmp/libqcperf-npu-0.1.0.0-aarch64.tar.gz -C /tmp
/tmp/libqcperf-npu-0.1.0.0-aarch64/install.sh
```

The installer puts the binary in `/usr/bin` and the shared library in
`/usr/lib`, then samples NPU0 and NPU1 once each and fails loudly if either
does not respond.

```
==> Installing to /usr
==> Verifying against NPU0
==> Verifying against NPU1
==> Install OK: /usr/bin/qcperf
```

`/usr/bin` is on the default PATH, so `qcperf` is callable by name with no
shell setup. The binary carries an RPATH of `$ORIGIN/../lib`, so it resolves
`/usr/lib/libqcperfCore.so` with no `LD_LIBRARY_PATH` set.

Some ostree images mount `/usr` read-only. The installer detects that, remounts
the filesystem read-write for the duration of the install, and restores the
read-only state on the way out, so it leaves no lingering change to the mount
table. Note that on an ostree system a later OS update replaces the `/usr`
tree, which removes the sampler; reinstall after an update.

Set `PREFIX` to install somewhere else. Any prefix with the same `bin`/`lib`
layout works, though outside `/usr` you have to call the binary by its full
path or extend PATH yourself.

```bash
PREFIX=/opt/innodisk/qcperf /tmp/libqcperf-npu-0.1.0.0-aarch64/install.sh
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

Sample NPU0 for three seconds:

```console
# qcperf 0 3
NPU_INDEX=0
Q6_UTIL=35.0678
Q6_CLOCK_KHZ=921600.0000
HVX_UTIL=26.1441
HMX_UTIL=5.9012
SAMPLES=14
```

Output fields:

| Key | Unit | Meaning |
|---|---|---|
| `NPU_INDEX` | — | which NSP was sampled |
| `Q6_UTIL` | % | Hexagon Q6 scalar core utilization |
| `Q6_CLOCK_KHZ` | KHz | Q6 clock frequency |
| `HVX_UTIL` | % | vector unit utilization |
| `HMX_UTIL` | % | matrix unit utilization |
| `SAMPLES` | count | readings averaged, after the warm-up reading is dropped |

The first reading after the sysmon session opens measures the session setup
rather than the workload, so it is discarded. That means a window must be long
enough to produce at least two readings; with the default 200 ms rate, one
second is already enough.

The backend holds a single sysmon session per process, so one process monitors
one NSP. To watch both, run the tool twice:

```bash
for npu in 0 1; do
    qcperf "$npu" 5
done
```

Because the output is plain `KEY=VALUE`, a shell can consume it with no parser:

```bash
eval "$(qcperf 0 5)"
echo "NPU0 Q6 utilization: ${Q6_UTIL}%"
```

The tool exits zero only when it collected at least one usable sample.
Diagnostics go to stderr prefixed with `[qcperf]`, keeping stdout clean for
the `KEY=VALUE` block.

## Troubleshooting

**`qcperf: command not found` after a successful install.** The default prefix
puts the binary on PATH, so this means it was installed under a custom
`PREFIX`. Call it by its full path, or add that prefix's `bin` to PATH.

**`connect NPU<n> failed`.** The sysmon skeleton library or the fastrpc device
node is missing. Check `/usr/lib/dsp/cdsp/libsysmonquery_skel.so` and
`/dev/fastrpc-cdsp` for NPU0, and the `cdsp1` equivalents for NPU1.

**`start failed` mentioning the sampling rate.** `sampling_rate_ms` accepts
only 1, 5, 10, 50, 100 and 200.

**`no samples collected`.** The window closed before a second reading arrived.
Use a longer window or a faster sampling rate.

**Staging fails with `qcperf was not built`.** The NPU backend was skipped at
configure time, which on an x86_64 host means the OE SDK was not found. Check
the configure log above the error and confirm `OECORE_DIR`.
