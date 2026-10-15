/*
 * Host-runnable test for the kgsl sysfs readers: builds a fake sysfs tree in a
 * temp directory and checks parsing, including the failure paths.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qcom_linux_gpu_utils.h"

static int g_failures;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                            \
        }                                                            \
    } while (0)

static void write_node(const char* root, const char* name, const char* content) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE* fp = fopen(path, "w");
    if (fp == NULL) {
        perror(path);
        exit(EXIT_FAILURE);
    }
    fputs(content, fp);
    fclose(fp);
}

static void test_utilization_parses_percent_suffix(const char* root) {
    double v = -1.0;
    write_node(root, QCOM_LINUX_GPU_BUSY_NODE, "97 %\n");
    CHECK(qcom_linux_gpu_util_read_utilization(root, &v) == QC_PERF_RETURN_CODE_SUCCESS);
    CHECK(fabs(v - 97.0) < 1e-9);
}

static void test_utilization_rejects_garbage(const char* root) {
    double v = -1.0;
    write_node(root, QCOM_LINUX_GPU_BUSY_NODE, "n/a\n");
    CHECK(qcom_linux_gpu_util_read_utilization(root, &v) == QC_PERF_RETURN_CODE_FAILED);
}

static void test_clock_converts_hz_to_mhz(const char* root) {
    double v = -1.0;
    write_node(root, QCOM_LINUX_GPU_CLOCK_NODE, "405000000\n");
    CHECK(qcom_linux_gpu_util_read_clock_mhz(root, &v) == QC_PERF_RETURN_CODE_SUCCESS);
    CHECK(fabs(v - 405.0) < 1e-9);
}

static void test_missing_node_fails(void) {
    double v = -1.0;
    CHECK(qcom_linux_gpu_util_read_utilization("/nonexistent/kgsl", &v) == QC_PERF_RETURN_CODE_FAILED);
    CHECK(qcom_linux_gpu_util_read_clock_mhz("/nonexistent/kgsl", &v) == QC_PERF_RETURN_CODE_FAILED);
}

static void test_null_arguments_rejected(const char* root) {
    double v = 0.0;
    CHECK(qcom_linux_gpu_util_read_utilization(NULL, &v) == QC_PERF_RETURN_CODE_NULL_POINTER);
    CHECK(qcom_linux_gpu_util_read_utilization(root, NULL) == QC_PERF_RETURN_CODE_NULL_POINTER);
    CHECK(qcom_linux_gpu_util_read_clock_mhz(NULL, &v) == QC_PERF_RETURN_CODE_NULL_POINTER);
    CHECK(qcom_linux_gpu_util_read_clock_mhz(root, NULL) == QC_PERF_RETURN_CODE_NULL_POINTER);
}

int main(void) {
    char root[] = "/tmp/qcperf_gpu_test_XXXXXX";
    if (mkdtemp(root) == NULL) {
        perror("mkdtemp");
        return EXIT_FAILURE;
    }

    test_utilization_parses_percent_suffix(root);
    test_utilization_rejects_garbage(root);
    test_clock_converts_hz_to_mhz(root);
    test_missing_node_fails();
    test_null_arguments_rejected(root);

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
    if (system(cmd) != 0) {
        fprintf(stderr, "warning: could not remove %s\n", root);
    }

    puts(g_failures == 0 ? "ALL PASS" : "FAILED");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
