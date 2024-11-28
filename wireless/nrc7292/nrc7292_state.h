/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_state.h
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

#ifndef __APPS_WIRELESS__NRC7292_NRC7292_STATE_H
#define __APPS_WIRELESS__NRC7292_NRC7292_STATE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <string.h>
#include <stdint.h>
#include <debug.h>
#include <errno.h>

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

void update_wireless_state(FAR struct nrc7292_s *priv,
                           enum wireless_conn_state_e state);

void update_socket_state(FAR struct nrc7292_s *priv, int socket_id,
                         enum sock_state_e next_state,
                         int argc, FAR char *argv[]);

void update_bind_state(FAR struct socket_info_s *sinfo,
                       enum sock_bind_state_e bind_state);

void update_scan_state(FAR struct nrc7292_s *priv,
                       enum scan_state_e next_state);

#endif /* __APPS_WIRELESS__NRC7292_NRC7292_STATE_H */
