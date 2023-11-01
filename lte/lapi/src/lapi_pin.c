/****************************************************************************
 * apps/lte/lapi/src/lapi_pin.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <nuttx/wireless/lte/lte_ioctl.h>

#include "lte/lte_api.h"
#include "lte/lapi.h"

#include "lapi_dbg.h"
#include "lapi_util.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Synchronous APIs */

int lte_get_pinset_sync(lte_getpin_t *pinset)
{
  return -ENOTSUP;
}

int lte_set_pinenable_sync(bool enable, char *pincode,
  uint8_t *attemptsleft)
{
  return -ENOTSUP;
}

int lte_change_pin_sync(int8_t target_pin, char *pincode,
  char *new_pincode, uint8_t *attemptsleft)
{
  return -ENOTSUP;
}

int lte_enter_pin_sync(char *pincode, char *new_pincode,
  uint8_t *simstat, uint8_t *attemptsleft)
{
  return -ENOTSUP;
}

/* Asynchronous APIs */

#ifdef CONFIG_LTE_LAPI_ENABLE_DEPRECATED_API

int lte_get_pinset(get_pinset_cb_t callback)
{
  return -ENOTSUP;
}

int lte_set_pinenable(bool enable, char *pincode,
  set_pinenable_cb_t callback)
{
  return -ENOTSUP;
}

int lte_change_pin(int8_t target_pin, char *pincode,
  char *new_pincode, change_pin_cb_t callback)
{
  return -ENOTSUP;
}

int lte_enter_pin(char *pincode, char *new_pincode,
  enter_pin_cb_t callback)
{
  return -ENOTSUP;
}
#endif /* CONFIG_LTE_LAPI_ENABLE_DEPRECATED_API */

