/****************************************************************************
 * apps/include/lte/lte_fwupdate.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __APPS_INCLUDE_LTE_LTE_FW_API_H
#define __APPS_INCLUDE_LTE_LTE_FW_API_H

/**
 * @brief LTE library for LTE firmware update
 *
 * API call type
 *
 * |     Sync API                  |
 * | ----------------------------- |
 * | ltefwupdate_initialize        |
 * | ltefwupdate_injectrest        |
 * | ltefwupdate_injected_datasize |
 * | ltefwupdate_execute           |
 * | ltefwupdate_result            |
 *
 * @attention
 * This API notifies the progress of the update by the callback set by
 * lte_set_report_restart(). You must call lte_set_report_restart()
 * before lte_power_on().
 * @{
 * @file  lte_fwupdate.h
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <nuttx/wireless/lte/lte.h>

#include "lte_fw_def.h"

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/** @name Functions */
/** @{ */

/**
 * Initialze injection delta image to LTE modem.
 *
 * Initialize LTE modem delta image injection with some data of top of delta
 * image.
 *
 * @param [in] initial_data: Pointer to top data of update image.
 * @param [in] len: Size of initial_data.
 *
 * @return Positive value is the injected length. Negative value is
 *         any error. In error case, the value can be below values.
 * - @ref LTEFW_RESULT_NOT_ENOUGH_INJECTSTORAGE
 * - @ref LTEFW_RESULT_DELTAIMAGE_HDR_CRC_ERROR
 * - @ref LTEFW_RESULT_DELTAIMAGE_HDR_UNSUPPORTED
 * - EINVAL
 * - ENODATA
 *
 */

int ltefwupdate_initialize(const char *initial_data, int len);

/**
 * Inject rest delta image to LTE modem.
 *
 * Inject the rest of the delta image following the data injected
 * by the ltefwupdate_initialize() and ltefwupdate_injectrest() functions.
 *
 * @param [in] rest_data: Pointer to rest data of update image.
 * @param [in] len: Size of rest_data.
 *
 * @return Positive value is the injected length. Negative value is
 *         any error. In error case, the value can be below values.
 * - @ref LTEFW_RESULT_NOT_ENOUGH_INJECTSTORAGE
 * - @ref LTEFW_RESULT_DELTAIMAGE_HDR_CRC_ERROR
 * - @ref LTEFW_RESULT_DELTAIMAGE_HDR_UNSUPPORTED
 * - EINVAL
 * - ENODATA
 *
 */

int ltefwupdate_injectrest(const char *rest_data, int len);

/**
 * Get length of injected delta image file.
 *
 * @return On success, The length of injected data to the modem is returned.
 * On failure, a negative value is returned according to <errno.h>.
 */

int ltefwupdate_injected_datasize(void);

/**
 * Execute delta update.
 * @attention When this function is executed, the modem is automatically
 * rebooted multiple times. The progress of the update can be checked by
 * the callback set by lte_set_report_restart().
 * Before executing this function, the modem must be woken up using
 * lte_acquire_wakelock() to safely update the modem. Then
 * lte_release_wakelock() is executed when the callback set by
 * lte_set_report_restart() is called.
 *
 * @return On success, 0 is returned. On failure,
 * negative value is returned as below values.
 * - @ref LTEFW_RESULT_PRECHK_SET_DELTAIMAGE_FAILED
 * - @ref LTEFW_RESULT_PRECHK_DELTAIMAGE_MISSING
 * - @ref LTEFW_RESULT_PRECHK_OOM
 * - @ref LTEFW_RESULT_PRECHK_SIZE_ERROR
 * - @ref LTEFW_RESULT_PRECHK_PKG_ERROR
 * - @ref LTEFW_RESULT_PRECHK_CRC_ERROR
 * - -EPERM
 *
 */

int ltefwupdate_execute(void);

/**
 * Get the result of delta update.
 * Execute this function after LTE_RESTART_MODEM_UPDATED is
 * notified to the callback set by lte_set_report_restart().
 *
 * @return On success, 0 is returned. On failure,
 * negative value is returned as below values.
 * - @ref LTEFW_RESULT_OK
 * - @ref LTEFW_RESULT_DELTAUPDATE_FAILED
 * - @ref LTEFW_RESULT_DELTAUPDATE_NORESULT
 *
 */

int ltefwupdate_result(void);

/** @} */

/** @} */

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __APPS_INCLUDE_LTE_LTE_FW_API_H */
