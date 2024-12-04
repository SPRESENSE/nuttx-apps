/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_regdom.h
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

#ifndef __APPS_WIRELESS__NRC7292_NRC7292_REGDOM_H
#define __APPS_WIRELESS__NRC7292_NRC7292_REGDOM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <string.h>
#include <stdint.h>
#include <debug.h>
#include <errno.h>

#include "nrc7292_daemon.h"

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

bool is_valid_country(FAR const char *country,
                      FAR struct board_info_s *info);
bool is_valid_freq(FAR const char *country,
                   uint32_t freq, uint32_t bandwidth);

#endif /* __APPS_WIRELESS__NRC7292_NRC7292_REGDOM_H */
