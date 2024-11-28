/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_ioctl.h
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

#ifndef __APPS_WIRELESS__NRC7292_NRC7292_IOCTL_H
#define __APPS_WIRELESS__NRC7292_NRC7292_IOCTL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <string.h>
#include <stdint.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/net/usrsock.h>

#include "nrc7292_daemon.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Data Types
 ****************************************************************************/

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

void set_ipinfo_to_driver(FAR struct nrc7292_s *priv, bool notif);
void clear_driver_ipaddr(FAR struct nrc7292_s *priv);

int handle_ioctl_request(int fd, FAR struct nrc7292_s *priv,
                         FAR struct usrsock_request_ioctl_s *req);

#endif /* __APPS_WIRELESS__NRC7292_NRC7292_IOCTL_H */
