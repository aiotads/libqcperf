# ============================================================================
# QcPerf Qualcomm Linux GPU Backend
# ============================================================================
# Reports Adreno GPU utilization and clock from the kgsl sysfs nodes.

message("Added qcom-linux-gpu backend")

add_library(QcPerfQcomLinuxGpuBackend STATIC
    src/qcom_linux_gpu.c
    src/qcom_linux_gpu_info.c
    src/qcom_linux_gpu_utils.c
)

target_include_directories(QcPerfQcomLinuxGpuBackend PUBLIC
    inc
    ${CMAKE_SOURCE_DIR}/core/inc
    ${CMAKE_SOURCE_DIR}/core/inc/internal
    ${CMAKE_SOURCE_DIR}/backends/inc
    ${CMAKE_SOURCE_DIR}/utils/qthread/inc
    ${CMAKE_SOURCE_DIR}/utils/qtime/inc
)

target_link_libraries(QcPerfQcomLinuxGpuBackend
    QcPerfQThread
    QcPerfQTime
    ${_OS_M}
)

# Host-runnable test of the sysfs readers; opt in with -DQCPERF_BUILD_TESTS=ON.
if(QCPERF_BUILD_TESTS)
    add_executable(qcom_linux_gpu_utils_test
        test/test_gpu_utils.c
        src/qcom_linux_gpu_utils.c
    )
    target_include_directories(qcom_linux_gpu_utils_test PRIVATE
        inc
        ${CMAKE_SOURCE_DIR}/core/inc
        ${CMAKE_SOURCE_DIR}/backends/inc
    )
    target_link_libraries(qcom_linux_gpu_utils_test ${_OS_M})
    add_test(NAME qcom_linux_gpu_utils COMMAND qcom_linux_gpu_utils_test)
endif()
