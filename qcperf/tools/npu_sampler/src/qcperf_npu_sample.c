/*
 * qcperf - one-shot NSP and GPU utilization sampler built on libqcperf (was qcperf-npu-sample).
 *
 * Averages the DSP NPU metrics, and the GPU metrics when the GPU backend is
 * built in, over a fixed window and prints them as KEY=VALUE lines, so shell
 * monitors can consume it without parsing the decorated output of QcPerfCoreTest.
 *
 * The backend holds a single sysmon session, so one process monitors one NSP:
 * run it once per NPU rather than trying to sample both from one process.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qcperf.h"
#include "qcperf_common.h"
#include "qcom_dsp_npu.h"
#ifdef QCPERF_ENABLED_QCOM_LINUX_GPU
#include "qcom_linux_gpu_info.h"
#endif

#define METRIC_COUNT 4

static double g_sum[METRIC_COUNT];
static unsigned long g_count[METRIC_COUNT];
static unsigned long g_seen[METRIC_COUNT];

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

static enum QcPerfReturnCode data_callback(struct QcPerfData* data) {
    if (NULL == data || NULL == data->metric_response) {
        return QC_PERF_RETURN_CODE_FAILED;
    }
    for (uint32_t i = 0; i < data->metric_response_len; i++) {
        const struct QcPerfMetricResponse* r = &data->metric_response[i];
        if (r->metric_id >= METRIC_COUNT) {
            continue;
        }
        if (r->metric_value.data_type != QC_PERF_DATA_TYPE_DOUBLE) {
            continue;
        }
        if (g_seen[r->metric_id]++ < WARMUP_SAMPLES) {
            continue;
        }
        g_sum[r->metric_id] += r->metric_value.double_value;
        g_count[r->metric_id]++;
    }
    return QC_PERF_RETURN_CODE_SUCCESS;
}

static enum QcPerfReturnCode message_callback(struct QcPerfMessage* message);

#ifdef QCPERF_ENABLED_QCOM_LINUX_GPU
#define GPU_METRIC_COUNT LINUX_GPU_CAPABILITY_0_METRIC_COUNT

static double g_gpu_sum[GPU_METRIC_COUNT];
static unsigned long g_gpu_count[GPU_METRIC_COUNT];

static const char* const GPU_METRIC_KEY[GPU_METRIC_COUNT] = {
    [LINUX_GPU_METRIC_UTILIZATION] = "GPU_UTIL",      /* % */
    [LINUX_GPU_METRIC_CLOCK]       = "GPU_CLOCK_MHZ", /* MHz */
};

static enum QcPerfReturnCode gpu_data_callback(struct QcPerfData* data) {
    if (NULL == data || NULL == data->metric_response) {
        return QC_PERF_RETURN_CODE_FAILED;
    }
    for (uint32_t i = 0; i < data->metric_response_len; i++) {
        const struct QcPerfMetricResponse* r = &data->metric_response[i];
        if (r->metric_id >= GPU_METRIC_COUNT || r->metric_value.data_type != QC_PERF_DATA_TYPE_DOUBLE) {
            continue;
        }
        g_gpu_sum[r->metric_id] += r->metric_value.double_value;
        g_gpu_count[r->metric_id]++;
    }
    return QC_PERF_RETURN_CODE_SUCCESS;
}

/* The GPU is an add-on to the NPU reading: if it cannot be monitored, say so
   on stderr and keep going rather than failing the NPU sample. */
static bool gpu_start(struct QcPerfRequest* request) {
    bool ok = false;

    if (QC_PERF_RETURN_CODE_SUCCESS != qcperf_connect_backend(QC_PERF_BACKEND_QCOM_LINUX_GPU, message_callback)) {
        fprintf(stderr, "[qcperf] GPU unavailable, reporting NPU only\n");
    } else if (QC_PERF_RETURN_CODE_SUCCESS != qcperf_set_data_callback(QC_PERF_BACKEND_QCOM_LINUX_GPU, gpu_data_callback) ||
               QC_PERF_RETURN_CODE_SUCCESS != qcperf_start(QC_PERF_BACKEND_QCOM_LINUX_GPU, request)) {
        fprintf(stderr, "[qcperf] GPU start failed, reporting NPU only\n");
        qcperf_disconnect_backend(QC_PERF_BACKEND_QCOM_LINUX_GPU);
    } else {
        ok = true;
    }
    return ok;
}

static void gpu_stop(struct QcPerfRequest* request) {
    qcperf_stop(QC_PERF_BACKEND_QCOM_LINUX_GPU, request);
    qcperf_disconnect_backend(QC_PERF_BACKEND_QCOM_LINUX_GPU);
}

static void gpu_print(void) {
    for (int m = 0; m < GPU_METRIC_COUNT; m++) {
        if (g_gpu_count[m] > 0) {
            printf("%s=%.4f\n", GPU_METRIC_KEY[m], g_gpu_sum[m] / (double)g_gpu_count[m]);
        }
    }
}
#endif /* QCPERF_ENABLED_QCOM_LINUX_GPU */

static enum QcPerfReturnCode message_callback(struct QcPerfMessage* message) {
    /* Only surface real problems; INFO/DEBUG would pollute the KEY=VALUE output. */
    if (NULL != message && NULL != message->message &&
        message->message_level == QC_PERF_MESSAGE_LEVEL_ERROR) {
        fprintf(stderr, "[qcperf] %s\n", message->message);
    }
    return QC_PERF_RETURN_CODE_SUCCESS;
}

static void usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s <npu_index> <profile_seconds> [sampling_rate_ms]\n"
        "  Also reports GPU_UTIL / GPU_CLOCK_MHZ when the GPU backend is built in.\n"
        "  npu_index         0 = NPU0 (cdsp), 1 = NPU1 (cdsp1)\n"
        "  profile_seconds   averaging window, >= 1\n"
        "  sampling_rate_ms  one of 1, 5, 10, 50, 100, 200 (default 200)\n",
        argv0);
}

int main(int argc, char** argv) {
    int npu_index = 0;
    int seconds = 0;
    int sampling_rate = 200;
    enum QcPerfReturnCode rc;
    struct QcPerfRequest request;
    int exit_code = EXIT_FAILURE;
    bool connected = false;
    bool started = false;
#ifdef QCPERF_ENABLED_QCOM_LINUX_GPU
    bool gpu_started = false;
#endif

    if (argc < 3 || argc > 4) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    npu_index = atoi(argv[1]);
    seconds = atoi(argv[2]);
    if (argc == 4) {
        sampling_rate = atoi(argv[3]);
    }
    if ((npu_index != 0 && npu_index != 1) || seconds < 1) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    rc = qcperf_dsp_npu_select((unsigned int)npu_index);
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf] invalid npu_index %d\n", npu_index);
        return EXIT_FAILURE;
    }

    rc = qcperf_init();
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf] qcperf_init failed: %d\n", rc);
        return EXIT_FAILURE;
    }

    rc = qcperf_connect_backend(QC_PERF_BACKEND_DSP_NPU, message_callback);
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf] connect NPU%d failed: %d\n", npu_index, rc);
        goto cleanup;
    }
    connected = true;

    rc = qcperf_set_data_callback(QC_PERF_BACKEND_DSP_NPU, data_callback);
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf] set_data_callback failed: %d\n", rc);
        goto cleanup;
    }

    memset(&request, 0, sizeof(request));
    request.capability_id = 0;
    request.sampling_rate = (uint16_t)sampling_rate;
    request.streaming_rate = 1000;

    rc = qcperf_start(QC_PERF_BACKEND_DSP_NPU, &request);
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf] start failed: %d "
                        "(sampling_rate must be 1/5/10/50/100/200)\n", rc);
        goto cleanup;
    }
    started = true;

#ifdef QCPERF_ENABLED_QCOM_LINUX_GPU
    gpu_started = gpu_start(&request);
#endif

    sleep((unsigned int)seconds);

    exit_code = EXIT_SUCCESS;

cleanup:
#ifdef QCPERF_ENABLED_QCOM_LINUX_GPU
    if (gpu_started) {
        gpu_stop(&request);
    }
#endif
    if (started) {
        qcperf_stop(QC_PERF_BACKEND_DSP_NPU, &request);
    }
    if (connected) {
        qcperf_disconnect_backend(QC_PERF_BACKEND_DSP_NPU);
    }
    qcperf_deinit();

    if (exit_code == EXIT_SUCCESS) {
        if (g_count[0] == 0) {
            fprintf(stderr, "[qcperf] no samples collected for NPU%d "
                            "(need more than %d; try a longer window)\n",
                    npu_index, WARMUP_SAMPLES);
            return EXIT_FAILURE;
        }
        printf("NPU_INDEX=%d\n", npu_index);
        for (int m = 0; m < METRIC_COUNT; m++) {
            if (g_count[m] > 0) {
                printf("%s=%.4f\n", METRIC_KEY[m], g_sum[m] / (double)g_count[m]);
            }
        }
        printf("SAMPLES=%lu\n", g_count[0]);
#ifdef QCPERF_ENABLED_QCOM_LINUX_GPU
        gpu_print();
#endif
    }
    return exit_code;
}
