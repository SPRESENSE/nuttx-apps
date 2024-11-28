/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_socket.h
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

#ifndef __APPS_WIRELESS__NRC7292_NRC7292_SOCKET_H
#define __APPS_WIRELESS__NRC7292_NRC7292_SOCKET_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <string.h>
#include <stdint.h>
#include <debug.h>
#include <errno.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define UDP_PAYLOAD_MAX 1470

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

void update_buffered_packet_size(FAR struct nrc7292_s *priv, int socket_id,
                                 size_t buffered_len);

uint32_t get_buffered_packet_size(FAR struct nrc7292_s *priv, int socket_id);

void nrc7292_socket_info_init(struct socket_info_s *sinfo);

int16_t nrc7292_socket_alloc(FAR struct nrc7292_s *priv,
                             int domain, int type);

FAR struct socket_info_s *nrc7292_socket_get(FAR struct nrc7292_s *priv,
                                             int sockid);

FAR struct socket_info_s *
nrc7292_socket_get_by_socketid(FAR struct nrc7292_s *priv,
                               int socket_id);
FAR struct socket_info_s *
nrc7292_socket_get_by_localport(FAR struct nrc7292_s *priv,
                                uint16_t lport);

int nrc7292_socket_free(FAR struct nrc7292_s *priv,
                        FAR struct socket_info_s *sinfo);

int nrc7292_socket_free_all(FAR struct nrc7292_s *priv);

void invalidate_all_sockets(FAR struct nrc7292_s *priv);

int handle_socket_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_socket_s *req);

int handle_connect_request(int fd, FAR struct nrc7292_s *priv,
                           FAR struct usrsock_request_connect_s *req);

int handle_bind_request(int fd, FAR struct nrc7292_s *priv,
                        FAR struct usrsock_request_bind_s *req);

int handle_listen_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_listen_s *req);

int handle_accept_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_accept_s *req);

int handle_setsockopt_request(int fd, FAR struct nrc7292_s *priv,
                              FAR struct usrsock_request_setsockopt_s *req);

int handle_getsockopt_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_getsockopt_s *req);

int handle_getsockname_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_getsockname_s *req);

int handle_getpeername_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_getpeername_s *req);

int handle_sendto_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_sendto_s *req);

int handle_recvfrom_request(int fd, FAR struct nrc7292_s *priv,
                            FAR struct usrsock_request_recvfrom_s *req);

int handle_close_request(int fd, FAR struct nrc7292_s *priv,
                         FAR struct usrsock_request_close_s *req);

int handle_shutdown_request(int fd, FAR struct nrc7292_s *priv,
                            FAR struct usrsock_request_shutdown_s *req);

#endif /* __APPS_WIRELESS__NRC7292_NRC7292_SOCKET_H */
