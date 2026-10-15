/*
 * qcperf-gpu - one-shot GPU sampler built on libqcperf.
 *
 * Averages GPU utilization and clock over a fixed window and prints them as
 * KEY=VALUE lines, the same shape as the NPU sampler (qcperf).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qcperf.h"
#include "qcperf_common.h"
#include "qcom_linux_gpu_info.h"

#define METRIC_COUNT LINUX_GPU_CAPABILITY_0_METRIC_COUNT

static double g_sum[METRIC_COUNT];
static unsigned long g_count[METRIC_COUNT];

static const char* const METRIC_KEY[METRIC_COUNT] = {
    [LINUX_GPU_METRIC_UTILIZATION] = "GPU_UTIL",      /* % */
    [LINUX_GPU_METRIC_CLOCK]       = "GPU_CLOCK_MHZ", /* MHz */
};

static enum QcPerfReturnCode data_callback(struct QcPerfData* data) {
    if (NULL == data || NULL == data->metric_response) {
        return QC_PERF_RETURN_CODE_FAILED;
    }
    for (uint32_t i = 0; i < data->metric_response_len; i++) {
        const struct QcPerfMetricResponse* r = &data->metric_response[i];
        if (r->metric_id >= METRIC_COUNT || r->metric_value.data_type != QC_PERF_DATA_TYPE_DOUBLE) {
            continue;
        }
        g_sum[r->metric_id] += r->metric_value.double_value;
        g_count[r->metric_id]++;
    }
    return QC_PERF_RETURN_CODE_SUCCESS;
}

static enum QcPerfReturnCode message_callback(struct QcPerfMessage* message) {
    /* Only surface real problems; INFO/DEBUG would pollute the KEY=VALUE output. */
    if (NULL != message && NULL != message->message && message->message_level == QC_PERF_MESSAGE_LEVEL_ERROR) {
        fprintf(stderr, "[qcperf-gpu] %s\n", message->message);
    }
    return QC_PERF_RETURN_CODE_SUCCESS;
}

static void usage(const char* argv0) {
    fprintf(stderr,
            "Usage: %s <profile_seconds> [sampling_rate_ms]\n"
            "  profile_seconds   averaging window, >= 1\n"
            "  sampling_rate_ms  one of 1, 5, 10, 50, 100, 200 (default 100)\n",
            argv0);
}

int main(int argc, char** argv) {
    int seconds       = 0;
    int sampling_rate = 100;
    enum QcPerfReturnCode rc;
    struct QcPerfRequest request;
    int exit_code  = EXIT_FAILURE;
    bool connected = false;
    bool started   = false;

    if (argc < 2 || argc > 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    seconds = atoi(argv[1]);
    if (argc == 3) {
        sampling_rate = atoi(argv[2]);
    }
    if (seconds < 1) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    rc = qcperf_init();
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf-gpu] qcperf_init failed: %d\n", rc);
        return EXIT_FAILURE;
    }

    rc = qcperf_connect_backend(QC_PERF_BACKEND_QCOM_LINUX_GPU, message_callback);
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf-gpu] connect failed: %d\n", rc);
        goto cleanup;
    }
    connected = true;

    rc = qcperf_set_data_callback(QC_PERF_BACKEND_QCOM_LINUX_GPU, data_callback);
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf-gpu] set_data_callback failed: %d\n", rc);
        goto cleanup;
    }

    memset(&request, 0, sizeof(request));
    request.capability_id  = LINUX_GPU_CAPABILITY_0_ID;
    request.sampling_rate  = (uint16_t)sampling_rate;
    request.streaming_rate = 1000;

    rc = qcperf_start(QC_PERF_BACKEND_QCOM_LINUX_GPU, &request);
    if (rc != QC_PERF_RETURN_CODE_SUCCESS) {
        fprintf(stderr, "[qcperf-gpu] start failed: %d (sampling_rate must be 1/5/10/50/100/200)\n", rc);
        goto cleanup;
    }
    started = true;

    sleep((unsigned int)seconds);

    exit_code = EXIT_SUCCESS;

cleanup:
    if (started) {
        qcperf_stop(QC_PERF_BACKEND_QCOM_LINUX_GPU, &request);
    }
    if (connected) {
        qcperf_disconnect_backend(QC_PERF_BACKEND_QCOM_LINUX_GPU);
    }
    qcperf_deinit();

    if (exit_code == EXIT_SUCCESS) {
        if (g_count[0] == 0) {
            fprintf(stderr, "[qcperf-gpu] no samples collected; try a longer window\n");
            return EXIT_FAILURE;
        }
        for (int m = 0; m < METRIC_COUNT; m++) {
            if (g_count[m] > 0) {
                printf("%s=%.4f\n", METRIC_KEY[m], g_sum[m] / (double)g_count[m]);
            }
        }
        printf("SAMPLES=%lu\n", g_count[0]);
    }
    return exit_code;
}
