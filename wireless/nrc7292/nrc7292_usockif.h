/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_usockif.h
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

#ifndef __APPS_WIRELESS__NRC7292_NRC7292_USOCKIF_H
#define __APPS_WIRELESS__NRC7292_NRC7292_USOCKIF_H

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

int send_usock_ack_common(int fd,
                          uint16_t events,
                          uint16_t flags,
                          uint32_t xid,
                          int16_t result);

int send_usock_ack_events(int fd,
                          uint16_t events,
                          uint32_t xid,
                          int16_t result);

int send_usock_ack_nodata(int fd,
                          uint32_t xid,
                          int16_t ret);

int send_usock_ack_in_progress(int fd,
                               uint32_t xid,
                               int16_t ret);

int send_usock_ack_with_data(int fd,
                             uint32_t xid,
                             int ret,
                             FAR uint8_t *data,
                             size_t data_len);

int send_usock_ack(FAR struct nrc7292_s *priv, int ret);

int send_usock_ack_fin(FAR struct nrc7292_s *priv, int ret);

int send_usock_ack_with_packet(int fd,
                               uint32_t xid,
                               int ret,
                               FAR uint8_t *addr,
                               size_t addr_len,
                               FAR uint8_t *data,
                               size_t data_len);

int send_usock_data_ack_with_addr(int fd,
                                  uint32_t xid,
                                  int ret,
                                  FAR uint8_t *addr,
                                  size_t addr_len,
                                  FAR uint8_t *data,
                                  size_t data_len);

int send_usock_socket_events(int fd,
                             int events,
                             int usockid);

int send_usock_socket_events(int fd,
                             int events,
                             int usockid);

#endif /* __APPS_WIRELESS__NRC7292_NRC7292_USOCKIF_H */
