add_executable(qcperf src/qcperf_npu_sample.c)

target_link_libraries(qcperf PRIVATE
    QcPerfCore
    QcPerfDspNpuBackend
)

# Resolve libqcperfCore.so relative to the binary so the install package needs
# neither ldconfig (the target has no /etc/ld.so.conf.d) nor LD_LIBRARY_PATH.
set_target_properties(qcperf PROPERTIES
    BUILD_RPATH "$ORIGIN:$ORIGIN/../lib"
    INSTALL_RPATH "$ORIGIN/../lib"
)

install(TARGETS qcperf RUNTIME DESTINATION bin)
install(TARGETS QcPerfCore LIBRARY DESTINATION lib)
