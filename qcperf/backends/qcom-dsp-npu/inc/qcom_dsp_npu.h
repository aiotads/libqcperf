/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    Redistribution and use in source and binary forms, with or without
    modification, are permitted (subject to the limitations in the
    disclaimer below) provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright
          notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above
          copyright notice, this list of conditions and the following
          disclaimer in the documentation and/or other materials provided
          with the distribution.
        * Neither the name of Qualcomm Technologies, Inc. nor the names of its
          contributors may be used to endorse or promote products derived
          from this software without specific prior written permission.
    NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
    GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
    HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
    ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
    GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
    IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
    OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
    IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * @file qcom_dsp_npu.h
 * @brief DSP NPU backend interface for libqcperf
 * @author  Snehal Lalage (slalage@qti.qualcomm.com)
 * This header provides the public interface for the DSP NPU backend, which
 * collects performance metrics from Qualcomm NPU/CDSP hardware using the
 * DSP library for real hardware monitoring capabilities.
 */

#ifndef QCOM_DSP_NPU_H
#define QCOM_DSP_NPU_H

#include "qcom_dsp_npu_info.h"

struct QcPerfBackendPrivate;

/**
 * @brief Create and initialize the DSP NPU backend
 *
 * This function configures the DSP NPU backend by populating the backend
 * private structure with function pointers for all required backend
 * operations including initialization, start, stop, deinitialization,
 * and callback registration.
 *
 * @param[in,out] backend Pointer to backend private structure to be configured
 *
 * @return QC_PERF_RETURN_CODE_SUCCESS on successful creation
 * @return QC_PERF_RETURN_CODE_NULL_POINTER if backend pointer is NULL
 */
enum QcPerfReturnCode qcperf_dsp_npu_create(struct QcPerfBackendPrivate* backend);

/**
 * @brief Select which NSP this process monitors, before connecting the backend.
 *
 * The backend keeps a single set of session state, so one process monitors one
 * NSP. Call this before qcperf_connect_backend(QC_PERF_BACKEND_DSP_NPU, ...);
 * calling it afterwards has no effect until the backend is reconnected.
 *
 * @param[in] npu_index 0 for NPU0 (cdsp), 1 for NPU1 (cdsp1)
 *
 * @return QC_PERF_RETURN_CODE_SUCCESS on success
 * @return QC_PERF_RETURN_CODE_INVALID_ARGUMENTS if npu_index is not 0 or 1
 */
__attribute__((visibility("default")))
enum QcPerfReturnCode qcperf_dsp_npu_select(unsigned int npu_index);

#endif /* QCOM_DSP_NPU_H */
