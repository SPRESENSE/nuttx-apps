/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_event.h
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

#ifndef __APPS_WIRELESS__NRC7292_NRC7292_EVENT_H
#define __APPS_WIRELESS__NRC7292_NRC7292_EVENT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <string.h>
#include <stdint.h>
#include <debug.h>
#include <errno.h>

/****************************************************************************
 * Public Data Types
 ****************************************************************************/

enum mon_event_e
{
  MONITOR_DHCP_START = 0,
  MONITOR_SOCKET_CLOSE,
  MONITOR_RESET_CTX,
  MONITOR_EVENT_MAX
};

struct monitor_args_s
{
  enum mon_event_e event;
};

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: daemon_event_thread
 ****************************************************************************/

int notify_event_to_daemon(int fd, enum mon_event_e event);

int notify_event_to_apps(FAR struct nrc7292_s *priv,
                         FAR struct wapi_event_cb_args_s *args);

FAR void *daemon_event_thread(FAR void *arg);

#endif /* __APPS_WIRELESS__NRC7292_NRC7292_EVENT_H */
