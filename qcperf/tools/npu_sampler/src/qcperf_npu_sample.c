/*
 * qcperf - one-shot NPU0, NPU1 and GPU utilization sampler built on libqcperf.
 *
 * Averages every metric over a fixed window and prints all of them as
 * KEY=VALUE lines, so shell monitors can grep what they need without parsing
 * the decorated output of QcPerfCoreTest:
 *
 *   NPU0_Q6_UTIL=...  NPU0_Q6_CLOCK_KHZ=...  NPU0_HVX_UTIL=...  NPU0_HMX_UTIL=...
 *   NPU0_SAMPLES=...  NPU1_* (same keys)     GPU_UTIL=...  GPU_CLOCK_MHZ=...
 *
 * The NPU backend holds a single sysmon session per process, so each NPU is
 * sampled by its own forked child. The children are forked before libqcperf is
 * initialised in this process, so no thread exists yet when fork() runs. The
 * GPU is sampled in the parent while the children run, and the three results
 * are printed in a fixed order once everything has finished.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qcperf.h"
#include "qcperf_common.h"
#include "qcom_dsp_npu.h"
#ifdef QCPERF_ENABLED_QCOM_LINUX_GPU
#include "qcom_linux_gpu_info.h"
#endif

#define NPU_COUNT 2
#define METRIC_COUNT 4
#define STREAMING_RATE_MS 1000
#define DEFAULT_SAMPLING_RATE_MS 200
#define OUTPUT_MAX 1024

/* The first reading after the sysmon session is opened covers the session
   setup itself, not the workload: it reads far above the steady state (e.g.
   16.8% against a 0.15% idle baseline, or 105% against a pegged 100.3%).
   Averaging it in skews short windows badly, so drop it. */
#define WARMUP_SAMPLES 1

static const char* const METRIC_KEY[METRIC_COUNT] = {
    "Q6_UTIL",       /* metric id 0, % */
    "Q6_CLOCK_KHZ",  /* metric id 1, KHz */
    "HVX_UTIL",      /* metric id 2, % */
    "HMX_UTIL",      /* metric id 3, % */
};

static const unsigned int SAMPLING_RATES_MS[] = {1, 5, 10, 50, 100, 200};

/* Accumulates one backend's readings. Each NPU child and the parent's GPU run
   own a separate copy, since they are separate processes or run in sequence. */
struct Accumulator {
    double sum[METRIC_COUNT];
    unsigned long count[METRIC_COUNT];
    unsigned long seen[METRIC_COUNT];
    uint32_t metric_count;
    unsigned long skip;
};

static struct Accumulator g_acc;

static enum QcPerfReturnCode data_callback(struct QcPerfData* data) {
    if (NULL == data || NULL == data->metric_response) {
        return QC_PERF_RETURN_CODE_FAILED;
    }
    for (uint32_t i = 0; i < data->metric_response_len; i++) {
        const struct QcPerfMetricResponse* r = &data->metric_response[i];
        if (r->metric_id >= g_acc.metric_count) {
            continue;
        }
        if (r->metric_value.data_type != QC_PERF_DATA_TYPE_DOUBLE) {
            continue;
        }
        if (g_acc.seen[r->metric_id]++ < g_acc.skip) {
            continue;
        }
        g_acc.sum[r->metric_id] += r->metric_value.double_value;
        g_acc.count[r->metric_id]++;
    }
    return QC_PERF_RETURN_CODE_SUCCESS;
}

static enum QcPerfReturnCode message_callback(struct QcPerfMessage* message) {
    /* Only surface real problems; INFO/DEBUG would pollute the KEY=VALUE output. */
    if (NULL != message && NULL != message->message &&
        message->message_level == QC_PERF_MESSAGE_LEVEL_ERROR) {
        fprintf(stderr, "[qcperf] %s\n", message->message);
    }
    return QC_PERF_RETURN_CODE_SUCCESS;
}

/* Sample one backend for `seconds` and leave the averages in g_acc. Returns
   true when the window was sampled; whether any readings arrived is for the
   caller to check. */
static bool sample_backend(enum QcPerfBackendId backend, const char* name, int seconds, int sampling_rate) {
    struct QcPerfRequest request;
    bool connected = false;
    bool started   = false;
    bool ok        = false;
    enum QcPerfReturnCode rc;

    rc = qcperf_init();
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf] %s: qcperf_init failed: %d\n", name, rc);
        return false;
    }

    rc = qcperf_connect_backend(backend, message_callback);
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf] connect %s failed: %d\n", name, rc);
    } else {
        connected = true;
        rc        = qcperf_set_data_callback(backend, data_callback);
        if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
            fprintf(stderr, "[qcperf] %s: set_data_callback failed: %d\n", name, rc);
        } else {
            memset(&request, 0, sizeof(request));
            request.capability_id  = 0;
            request.sampling_rate  = (uint16_t)sampling_rate;
            request.streaming_rate = STREAMING_RATE_MS;

            rc = qcperf_start(backend, &request);
            if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
                fprintf(stderr, "[qcperf] %s: start failed: %d\n", name, rc);
            } else {
                started = true;
                sleep((unsigned int)seconds);
                ok = true;
            }
        }
    }

    if (started) {
        qcperf_stop(backend, &request);
    }
    if (connected) {
        qcperf_disconnect_backend(backend);
    }
    qcperf_deinit();
    return ok;
}

/* Write one backend's averages as "<prefix>KEY=VALUE" lines. */
static void format_averages(FILE* out, const char* prefix, const char* const* keys, uint32_t metric_count) {
    for (uint32_t m = 0; m < metric_count; m++) {
        if (g_acc.count[m] > 0) {
            fprintf(out, "%s%s=%.4f\n", prefix, keys[m], g_acc.sum[m] / (double)g_acc.count[m]);
        }
    }
}

/* Body of one NPU child: sample the NPU and write its lines to `out`.
   Returns the child's exit status. */
static int run_npu_child(int npu_index, int seconds, int sampling_rate, FILE* out) {
    char name[16];
    char prefix[16];

    snprintf(name, sizeof(name), "NPU%d", npu_index);
    snprintf(prefix, sizeof(prefix), "NPU%d_", npu_index);

    if (qcperf_dsp_npu_select((unsigned int)npu_index) != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf] invalid npu_index %d\n", npu_index);
        return EXIT_FAILURE;
    }

    g_acc.metric_count = METRIC_COUNT;
    g_acc.skip         = WARMUP_SAMPLES;
    if (!sample_backend(QC_PERF_BACKEND_DSP_NPU, name, seconds, sampling_rate)) {
        return EXIT_FAILURE;
    }
    if (g_acc.count[0] == 0) {
        fprintf(stderr, "[qcperf] no samples collected for %s (need more than %d; try a longer window)\n", name, WARMUP_SAMPLES);
        return EXIT_FAILURE;
    }

    format_averages(out, prefix, METRIC_KEY, METRIC_COUNT);
    fprintf(out, "%sSAMPLES=%lu\n", prefix, g_acc.count[0]);
    return EXIT_SUCCESS;
}

#ifdef QCPERF_ENABLED_QCOM_LINUX_GPU
static const char* const GPU_METRIC_KEY[LINUX_GPU_CAPABILITY_0_METRIC_COUNT] = {
    [LINUX_GPU_METRIC_UTILIZATION] = "GPU_UTIL",      /* % */
    [LINUX_GPU_METRIC_CLOCK]       = "GPU_CLOCK_MHZ", /* MHz */
};

/* The GPU is reported on a best-effort basis: if it cannot be monitored, say
   so on stderr and leave its keys out rather than failing the NPU readings. */
static void run_gpu(int seconds, int sampling_rate, char* out, size_t out_size) {
    FILE* fp = fmemopen(out, out_size, "w");

    g_acc.metric_count = LINUX_GPU_CAPABILITY_0_METRIC_COUNT;
    g_acc.skip         = 0;
    if (NULL == fp) {
        fprintf(stderr, "[qcperf] GPU: cannot buffer output\n");
    } else if (sample_backend(QC_PERF_BACKEND_QCOM_LINUX_GPU, "GPU", seconds, sampling_rate)) {
        format_averages(fp, "", GPU_METRIC_KEY, LINUX_GPU_CAPABILITY_0_METRIC_COUNT);
    } else {
        fprintf(stderr, "[qcperf] GPU unavailable, reporting NPU only\n");
    }
    if (NULL != fp) {
        fclose(fp);
    }
}
#endif /* QCPERF_ENABLED_QCOM_LINUX_GPU */

static bool sampling_rate_valid(int rate) {
    for (size_t i = 0; i < sizeof(SAMPLING_RATES_MS) / sizeof(SAMPLING_RATES_MS[0]); i++) {
        if ((int)SAMPLING_RATES_MS[i] == rate) {
            return true;
        }
    }
    return false;
}

static void usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s <profile_seconds> [sampling_rate_ms]\n"
        "  Samples NPU0, NPU1 and the GPU over the same window and prints every\n"
        "  metric as a KEY=VALUE line (NPU0_*, NPU1_*, GPU_*).\n"
        "  profile_seconds   averaging window, >= 1\n"
        "  sampling_rate_ms  one of 1, 5, 10, 50, 100, 200 (default %d)\n",
        argv0, DEFAULT_SAMPLING_RATE_MS);
}

/* Fork one child per NPU. Each child writes its lines into its own pipe. */
static bool spawn_npu_children(int seconds, int sampling_rate, pid_t pids[NPU_COUNT], int read_fds[NPU_COUNT]) {
    for (int npu = 0; npu < NPU_COUNT; npu++) {
        int fds[2];

        if (pipe(fds) != 0) {
            perror("[qcperf] pipe");
            return false;
        }
        pids[npu] = fork();
        if (pids[npu] < 0) {
            perror("[qcperf] fork");
            close(fds[0]);
            close(fds[1]);
            return false;
        }
        if (pids[npu] == 0) {
            FILE* out = fdopen(fds[1], "w");
            int rc;

            close(fds[0]);
            for (int prev = 0; prev < npu; prev++) {
                close(read_fds[prev]);
            }
            rc = (NULL == out) ? EXIT_FAILURE : run_npu_child(npu, seconds, sampling_rate, out);
            if (NULL != out) {
                fclose(out);
            }
            _exit(rc);
        }
        close(fds[1]);
        read_fds[npu] = fds[0];
    }
    return true;
}

/* Copy everything the child wrote to stdout, then reap it. Returns whether the
   child exited cleanly. */
static bool collect_npu_child(pid_t pid, int read_fd) {
    char buf[OUTPUT_MAX];
    ssize_t n;
    int status = 0;

    while ((n = read(read_fd, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, (size_t)n, stdout);
    }
    close(read_fd);
    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    int seconds       = 0;
    int sampling_rate = DEFAULT_SAMPLING_RATE_MS;
    pid_t pids[NPU_COUNT];
    int read_fds[NPU_COUNT];
    int exit_code = EXIT_SUCCESS;
    char gpu_out[OUTPUT_MAX] = {0};

    if (argc < 2 || argc > 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    seconds = atoi(argv[1]);
    if (argc == 3) {
        sampling_rate = atoi(argv[2]);
    }
    if (seconds < 1 || !sampling_rate_valid(sampling_rate)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Fork before libqcperf starts any thread in this process. */
    if (!spawn_npu_children(seconds, sampling_rate, pids, read_fds)) {
        return EXIT_FAILURE;
    }

#ifdef QCPERF_ENABLED_QCOM_LINUX_GPU
    run_gpu(seconds, sampling_rate, gpu_out, sizeof(gpu_out));
#endif

    for (int npu = 0; npu < NPU_COUNT; npu++) {
        if (!collect_npu_child(pids[npu], read_fds[npu])) {
            fprintf(stderr, "[qcperf] NPU%d sampling failed\n", npu);
            exit_code = EXIT_FAILURE;
        }
    }
    fputs(gpu_out, stdout);
    return exit_code;
}
