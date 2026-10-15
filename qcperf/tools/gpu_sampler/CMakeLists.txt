add_executable(qcperf-gpu src/qcperf_gpu_sample.c)

target_link_libraries(qcperf-gpu PRIVATE
    QcPerfCore
    QcPerfQcomLinuxGpuBackend
)

# Resolve libqcperfCore.so relative to the binary, same as the NPU sampler.
set_target_properties(qcperf-gpu PROPERTIES
    BUILD_RPATH "$ORIGIN:$ORIGIN/../lib"
    INSTALL_RPATH "$ORIGIN/../lib"
)

install(TARGETS qcperf-gpu RUNTIME DESTINATION bin)
