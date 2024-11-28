/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_atcmd.h
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

#ifndef __APPS_WIRELESS__NRC7292_NRC7292_ATCMD_H
#define __APPS_WIRELESS__NRC7292_NRC7292_ATCMD_H

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
 * Pre-processor Definitions
 ****************************************************************************/

#define ATCMD_RESP_TOKEN_MAX 5

/****************************************************************************
 * Public Data Types
 ****************************************************************************/

struct atcmd_resp_tokens_s
{
  int  count;
  FAR char *tokens[ATCMD_RESP_TOKEN_MAX];
};

struct setup_cmd_s
{
  const char *cmd;
  const char *event;
  procresp_t resp_cb;
  procret_t  ret_cb;
};

enum atcmd_list_e
  {
    ATCMD_MODULE_SETUP = 0,
    ATCMD_ALL_SOCKET_CLOSE,
    ATCMD_DHCP_START,
    ATCMD_MAX
  };

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

FAR const struct setup_cmd_s *get_atcmd_list(enum atcmd_list_e id);
int split_atcmd_tokens(FAR char *input,
                       FAR struct atcmd_resp_tokens_s *resp);
void init_atcmd_ctx(FAR struct atcmd_ctx_s *ctx, int fd, uint32_t xid,
                    FAR struct socket_info_s *sinfo);
void deinit_atcmd_ctx(FAR struct atcmd_ctx_s *ctx);
void ignore_atcmd_response(FAR struct atcmd_ctx_s *ctx);
int send_atcmd(int fd, FAR char *cmd);
int send_atcmd_rawdata(int fd, FAR uint8_t *buf, size_t len);
int read_atcmd_todelim(int fd, FAR char *buf, FAR size_t *len, char delim);
int read_atcmd(int fd, FAR char *buf, FAR size_t *len);
int read_atcmd_char(int fd);
int procret_none(FAR struct nrc7292_s *priv, int ret);
void proc_atcmd_response(FAR struct nrc7292_s *priv,
                         FAR struct atcmd_resp_tokens_s *resp,
                         FAR const char *event);
void proc_atcmd_return(FAR struct nrc7292_s *priv, int retval);
int exec_atcmd(FAR struct atcmd_ctx_s *ctx, int fd,
               FAR const char *resp_event, procresp_t resp_cb,
               procret_t return_cb, FAR const char *atcmd, ...);

#endif /* __APPS_WIRELESS__NRC7292_NRC7292_ATCMD_H */
