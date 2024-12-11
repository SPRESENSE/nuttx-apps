/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_atcmd.c
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <nuttx/wireless/nrc7292.h>

#include "nrc7292_daemon.h"
#include "nrc7292_atcmd.h"
#include "nrc7292_ioctl.h"
#include "nrc7292_usockif.h"
#include "nrc7292_event.h"
#include "nrc7292_regdom.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define STR(x) #x
#define STRVALUE(x) STR(x)

#define ATCMD_SSEND_TIMEOUT \
  "AT+STIMEOUT=\"SSEND\","\
  STRVALUE(CONFIG_WIRELESS_NRC7292_ATSSEND_TIMEOUT)\
  "\r\n"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int procresp_getversion(FAR struct nrc7292_s *priv,
                               FAR struct atcmd_resp_tokens_s *resp);
static int procresp_gethwaddr(FAR struct nrc7292_s *priv,
                              FAR struct atcmd_resp_tokens_s *resp);
static int procresp_getcountry(FAR struct nrc7292_s *priv,
                               FAR struct atcmd_resp_tokens_s *resp);
static int procresp_sfsysuser(FAR struct nrc7292_s *priv,
                               FAR struct atcmd_resp_tokens_s *resp);
static int procresp_dhcp(FAR struct nrc7292_s *priv,
                         FAR struct atcmd_resp_tokens_s *resp);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct setup_cmd_s g_module_setup_cmds[] =
{
{
  "AT+VER?\r\n",
  "VER",
  procresp_getversion,
  procret_none
},
{
  "AT+SCLOSE\r\n",
  NULL,
  NULL,
  procret_none
},
{
  "AT+SRECVMODE=1,1\r\n",
  NULL,
  NULL,
  procret_none
},
{
  "AT+WTIMEOUT=\"WDHCP\",10\r\n",
  NULL,
  NULL,
  procret_none
},
{
  "AT+SRECVINFO=1\r\n",
  NULL,
  NULL,
  procret_none
},
{
  "AT+WMACADDR?\r\n",
  "WMACADDR",
  procresp_gethwaddr,
  procret_none
},
{
  "AT+SFSYSUSER=0,108\r\n",
  "RXD_SFSYSUSER",
  procresp_sfsysuser,
  procret_none
},
{
  "AT+WCOUNTRY?\r\n",
  "WCOUNTRY",
  procresp_getcountry,
  procret_none
},
{
  ATCMD_SSEND_TIMEOUT,
  NULL,
  NULL,
  procret_none
},
{
  "AT+WDUTYCYCLE=1000000,100000\r\n", /* Duty ratio 10%: window=1s, duration=0.1s */
  NULL,
  NULL,
  procret_none
},
{
  NULL,
  NULL,
  NULL,
  NULL,
},
};

static const struct setup_cmd_s g_close_sockets_cmds[] =
{
{
  "AT+SCLOSE\r\n",
  NULL,
  NULL,
  procret_none
},
{
  NULL,
  NULL,
  NULL,
  NULL,
},
};

static const struct setup_cmd_s g_dhcp_start_cmds[] =
{
{
  "AT+WDHCP\r\n",
  "WDHCP",
  procresp_dhcp,
  procret_none
},
{
  NULL,
  NULL,
  NULL,
  NULL,
},
};

static const struct setup_cmd_s *g_atcmd_table[] =
{
  g_module_setup_cmds,
  g_close_sockets_cmds,
  g_dhcp_start_cmds,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: procresp_getversion
 ****************************************************************************/

static int procresp_getversion(FAR struct nrc7292_s *priv,
                               FAR struct atcmd_resp_tokens_s *resp)
{
  if (1 < resp->count)
    {
      strncpy(priv->fw_ver.sdk,
              resp->tokens[0], sizeof(priv->fw_ver.sdk));
      strncpy(priv->fw_ver.atcmd,
              resp->tokens[1], sizeof(priv->fw_ver.atcmd));
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procresp_gethwaddr
 ****************************************************************************/

static int procresp_gethwaddr(FAR struct nrc7292_s *priv,
                              FAR struct atcmd_resp_tokens_s *resp)
{
  struct ifreq ifr;
  FAR char *mac;

  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, WLAN_IFNAME);
  mac = ifr.ifr_hwaddr.sa_data;

  if (0 < resp->count)
    {
      sscanf(resp->tokens[0], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    }

  memcpy(priv->macaddr, mac, 6);
  ioctl(priv->drv_fd, SIOCSIFHWADDR, &ifr);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procresp_getcountry
 ****************************************************************************/

static int procresp_getcountry(FAR struct nrc7292_s *priv,
                               FAR struct atcmd_resp_tokens_s *resp)
{
  if (0 < resp->count &&
      is_valid_country(resp->tokens[0], &priv->board_info))
    {
      strncpy(priv->country, resp->tokens[0], 2);
      priv->country[2] = '\0';
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: parse_board_info
 ****************************************************************************/

void parse_board_info(FAR struct nrc7292_s *priv,
                      FAR char *jstr)
{
  int i;

  jstr = strstr(jstr, "HW Version:") + strlen("HW Version: ");
  sscanf(jstr, "%s,", priv->board_info.hwver);

  for (i = 0; i < sizeof(priv->board_info.hwver); i++)
    {
      if (priv->board_info.hwver[i] == ',')
        {
          priv->board_info.hwver[i] = '\0';
          break;
        }
    }

  jstr = strstr(jstr, "Manufacturer:") + strlen("Manufacturer: ");
  sscanf(jstr, "%s,", priv->board_info.manufacturer);

  for (i = 0; i < sizeof(priv->board_info.manufacturer); i++)
    {
      if (priv->board_info.manufacturer[i] == ',')
        {
          priv->board_info.manufacturer[i] = '\0';
          break;
        }
    }

  jstr = strstr(jstr, "\"WiHaLow\", ") + strlen("\"WiHalLow\", ");
  strncpy(priv->board_info.regdom, jstr, 2);
  priv->board_info.regdom[2] = '\0';

  nrc7292_printf("HW Version:   %s\n", priv->board_info.hwver);
  nrc7292_printf("Manufacturer: %s\n", priv->board_info.manufacturer);
  nrc7292_printf("Regulatory  : %s\n", priv->board_info.regdom);
}

/****************************************************************************
 * Name: procresp_sfsysuser
 ****************************************************************************/

static int procresp_sfsysuser(FAR struct nrc7292_s *priv,
                              FAR struct atcmd_resp_tokens_s *resp)
{
  FAR char *buf = (FAR char *) priv->buf;
  ssize_t rem_len;
  ssize_t offset = 0;
  int ret;

  if (resp->count < 2)
    {
      goto error;
    }

  memset(buf, 0, BUFLEN);
  rem_len = strtoul(resp->tokens[1], NULL, 10);

  while (0 < rem_len)
    {
      ret = read(priv->drv_fd, buf + offset, rem_len);

      if (ret < 0)
        {
          if (errno != EAGAIN)
            {
              ret = -errno;
              goto error;
            }
          else
            {
              continue;
            }
        }

      rem_len -= ret;
      offset += ret;
    }

  parse_board_info(priv, buf);

error:
  return ATCMD_FIN;
}

/****************************************************************************
 * Name: notify_dhcp_timeout_event
 ****************************************************************************/

static void notify_dhcp_timeout_event(FAR struct nrc7292_s *priv)
{
  struct wapi_event_cb_args_s arg;

  memset(&arg, 0, sizeof(arg));
  arg.event =  WAPI_EVENT_DHCP_TIMEOUT;

  notify_event_to_apps(priv, &arg);
}

/****************************************************************************
 * Name: procresp_dhcp
 ****************************************************************************/

static int procresp_dhcp(FAR struct nrc7292_s *priv,
                         FAR struct atcmd_resp_tokens_s *resp)
{
  int error;

  if (resp->count == 1)
    {
      error = strtol(resp->tokens[0], NULL, 10);

      /* "4" means timeout */

      if (error == 4)
        {
          notify_dhcp_timeout_event(priv);
        }
    }
  else if (2 < resp->count)
    {
      priv->ifconfig.ipaddr.s_addr = inet_addr(resp->tokens[0]);
      priv->ifconfig.netmask.s_addr = inet_addr(resp->tokens[1]);
      priv->ifconfig.draddr.s_addr = inet_addr(resp->tokens[2]);

      set_ipinfo_to_driver(priv, true);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: get_atcmd_list
 ****************************************************************************/

FAR const struct setup_cmd_s *get_atcmd_list(enum atcmd_list_e id)
{
  if (id < ATCMD_MAX)
    {
      return g_atcmd_table[id];
    }

  return NULL;
}

/****************************************************************************
 * Name: split_atcmd_tokens
 ****************************************************************************/

int split_atcmd_tokens(FAR char *input, FAR struct atcmd_resp_tokens_s *resp)
{
  FAR char *token;
  int i;

  if (input == NULL || resp == NULL)
    {
      return 0;
    }

  if (input[0] == '\r' || input[0] == '\n' || input[0] == '\0')
    {
      return 0;
    }

  resp->count = 0;

  for (i = 0; i < ATCMD_RESP_TOKEN_MAX; i++)
    {
      resp->tokens[i] = NULL;
    }

  token = input;

  while (1)
    {
      if (*input == '"')
        {
          if (input == token)
            {
              token++;
            }
          else
            {
              *input = '\0';
            }
        }
      else if (*input == ',')
        {
          *input = '\0';
          resp->tokens[resp->count] = token;
          resp->count++;

          if (resp->count == (ATCMD_RESP_TOKEN_MAX))
            {
              break;
            }

          token = input + 1;
        }
      else if (*input == '\r' || *input == '\n' || *input == '\0')
        {
          resp->tokens[resp->count] = token;
          resp->count++;
          break;
        }

      input++;
    }

  return resp->count;
}

/****************************************************************************
 * Name: init_atcmd_ctx
 ****************************************************************************/

void init_atcmd_ctx(FAR struct atcmd_ctx_s *ctx, int fd, uint32_t xid,
                    FAR struct socket_info_s *sinfo)
{
  if (ctx == NULL)
    {
      return;
    }

  ctx->fd = fd;
  ctx->is_active = false;
  ctx->resp_received = false;
  ctx->ret_received = false;
  ctx->hook_event = NULL;
  ctx->sinfo = sinfo;
  ctx->procresp_cb = NULL;
  ctx->procret_cb = NULL;
  ctx->xid = xid;
  if (sinfo != NULL)
    {
      sinfo->xid = xid;
    }

  memset(&ctx->resp, 0, sizeof(ctx->resp));
  ctx->resp_len = 0;
  memset(&ctx->dst_addr, 0, sizeof(ctx->dst_addr));
  ctx->return_error_code = 0;
}

/****************************************************************************
 * Name: deinit_atcmd_ctx
 ****************************************************************************/

void deinit_atcmd_ctx(FAR struct atcmd_ctx_s *ctx)
{
  init_atcmd_ctx(ctx, -1, -1, NULL);
}

/****************************************************************************
 * Name: ignore_atcmd_response
 ****************************************************************************/

void ignore_atcmd_response(FAR struct atcmd_ctx_s *ctx)
{
  if (ctx != NULL)
    {
      ctx->resp_received = true;
    }
}

/****************************************************************************
 * Name: send_atcmd
 ****************************************************************************/

int send_atcmd(int fd, FAR char *cmd)
{
  size_t len;
  int written_len;
  int ret;

  if (cmd == NULL)
    {
      ret = -EINVAL;
      goto error;
    }

  len = strnlen(cmd, ATCMD_BUFLEN);

  if (2 <= len && (cmd[len - 2] == '\r' && cmd[len - 1] == '\n'))
    {
      written_len = write(fd, cmd, len);

      if (written_len < 0)
        {
          ret = -errno;
        }
      else
        {
          ret = OK;
        }
    }
  else
    {
      ret = -EINVAL;
    }

error:
  return ret;
}

/****************************************************************************
 * Name: send_atcmd_rawdata
 ****************************************************************************/

int send_atcmd_rawdata(int fd, FAR uint8_t *buf, size_t len)
{
  ssize_t written_len;
  int ret;

  if (buf == NULL || len == 0)
    {
      ret = -EINVAL;
      goto error;
    }

  written_len = write(fd, buf, len);

  if (written_len < 0)
    {
      ret = -errno;
    }
  else
    {
      ret = OK;
    }

error:
  return ret;
}

/****************************************************************************
 * Name: read_atcmd_todelim
 ****************************************************************************/

int read_atcmd_todelim(int fd, FAR char *buf, FAR size_t *len, char delim)
{
  char ch;
  int ret;
  size_t buf_len;
  ssize_t i;

  if (buf == NULL || len == NULL || *len == 0)
    {
      ret = -EINVAL;
      goto error;
    }

  buf_len = *len;
  i = 0;

  while (true)
    {
      ch = read_atcmd_char(fd);

      if (ch == EOF)
        {
          ret = -EIO;
          goto error;
        }
      else if (ch == delim)
        {
          buf[i] = '\0';
          i++;
          *len = i;
          ret = OK;
          break;
        }
      else if (isgraph(ch))
        {
          buf[i] = ch;
          i++;
        }

      if (buf_len < i)
        {
          ret = -ENOMEM;
          *len = i;
          break;
        }
    }

error:
  return ret;
}

/****************************************************************************
 * Name: read_atcmd
 ****************************************************************************/

int read_atcmd(int fd, FAR char *buf, size_t *len)
{
  return read_atcmd_todelim(fd, buf, len, '\n');
}

/****************************************************************************
 * Name: read_atcmd_char
 ****************************************************************************/

int read_atcmd_char(int fd)
{
  char    ch;
  int     ret;
  ssize_t read_len;

  read_len = read(fd, &ch, 1);

  if (read_len <= 0)
    {
      ret = EOF;
    }
  else if (0 < read_len)
    {
      ret = (int) ch;
    }

  return ret;
}

/****************************************************************************
 * Name: procret_none
 ****************************************************************************/

int procret_none(FAR struct nrc7292_s *priv, int ret)
{
  return ATCMD_FIN;
}

/****************************************************************************
 * Name: proc_atcmd_response
 ****************************************************************************/

void proc_atcmd_response(FAR struct nrc7292_s *priv,
                         FAR struct atcmd_resp_tokens_s *resp,
                         FAR const char *event)
{
  FAR struct atcmd_ctx_s *ctx;
  int ret;

  ctx = &priv->atcmd_ctx;

  if (ctx->hook_event != NULL
      && strncmp(event, ctx->hook_event, strlen(ctx->hook_event)) == 0)
    {
      if (ctx->procresp_cb != NULL)
        {
          ret = ctx->procresp_cb(priv, resp);

          ctx->resp_received = (ret == ATCMD_FIN);

          if (ctx->resp_received && ctx->ret_received)
            {
              deinit_atcmd_ctx(&priv->atcmd_ctx);
            }
        }
    }
}

/****************************************************************************
 * Name: procret_atcmd
 ****************************************************************************/

void proc_atcmd_return(FAR struct nrc7292_s *priv, int retval)
{
  FAR struct atcmd_ctx_s *ctx;
  int ret;

  ctx = &priv->atcmd_ctx;

  if (ctx->procret_cb != NULL)
    {
      ret = ctx->procret_cb(priv, retval);

      ctx->ret_received = (ret == ATCMD_FIN);

      if (ctx->resp_received && ctx->ret_received)
        {
          deinit_atcmd_ctx(&priv->atcmd_ctx);
        }
    }
}

/****************************************************************************
 * Name: exec_atcmd
 ****************************************************************************/

int exec_atcmd(FAR struct atcmd_ctx_s *ctx, int fd,
               FAR const char *resp_event, procresp_t resp_cb,
               procret_t return_cb, FAR const char *atcmd, ...)
{
  int ret;
  va_list ap;

  ctx->hook_event = resp_event;

  if (resp_cb == NULL)
    {
      ctx->resp_received = true;
    }

  ctx->resp_received = (resp_cb == NULL);

  if (return_cb == NULL)
    {
      ctx->ret_received = true;
    }

  ctx->ret_received = (return_cb == NULL);

  ctx->procresp_cb = resp_cb;
  ctx->procret_cb = return_cb;

  va_start(ap, atcmd);
  vsnprintf(ctx->atcmd_txbuf, ATCMD_BUFLEN,
           atcmd, ap);
  va_end(ap);

  ret = send_atcmd(fd, ctx->atcmd_txbuf);
  ctx->is_active = (0 <= ret);

  return ret;
}

