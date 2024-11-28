/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_main.c
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
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <nuttx/net/usrsock.h>
#include <nuttx/wireless/nrc7292.h>

#include "nrc7292_daemon.h"
#include "nrc7292_socket.h"
#include "nrc7292_ioctl.h"
#include "nrc7292_usockif.h"
#include "nrc7292_atcmd.h"
#include "nrc7292_state.h"
#include "nrc7292_event.h"

/****************************************************************************
 * Private Data Types
 ****************************************************************************/

union usrsock_request_u
{
  struct usrsock_request_socket_s       socket;
  struct usrsock_request_close_s        close;
  struct usrsock_request_connect_s      connect;
  struct usrsock_request_sendto_s       sendto;
  struct usrsock_request_recvfrom_s     recvfrom;
  struct usrsock_request_setsockopt_s   setsockopt;
  struct usrsock_request_getsockopt_s   getsockopt;
  struct usrsock_request_getsockname_s  getsockname;
  struct usrsock_request_getpeername_s  getpeername;
  struct usrsock_request_bind_s         bind;
  struct usrsock_request_listen_s       listen;
  struct usrsock_request_accept_s       accept;
  struct usrsock_request_ioctl_s        ioctl;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int socket_request(int fd, FAR struct nrc7292_s *priv,
                          FAR void *hdrbuf);
static int close_request(int fd, FAR struct nrc7292_s *priv,
                         FAR void *hdrbuf);
static int connect_request(int fd, FAR struct nrc7292_s *priv,
                           FAR void *hdrbuf);
static int sendto_request(int fd, FAR struct nrc7292_s *priv,
                          FAR void *hdrbuf);
static int recvfrom_request(int fd, FAR struct nrc7292_s *priv,
                            FAR void *hdrbuf);
static int setsockopt_request(int fd, FAR struct nrc7292_s *priv,
                              FAR void *hdrbuf);
static int getsockopt_request(int fd, FAR struct nrc7292_s *priv,
                              FAR void *hdrbuf);
static int getsockname_request(int fd, FAR struct nrc7292_s *priv,
                               FAR void *hdrbuf);
static int getpeername_request(int fd, FAR struct nrc7292_s *priv,
                               FAR void *hdrbuf);
static int ioctl_request(int fd, FAR struct nrc7292_s *priv,
                         FAR void *hdrbuf);
static int bind_request(int fd, FAR struct nrc7292_s *priv,
                        FAR void *hdrbuf);
static int listen_request(int fd, FAR struct nrc7292_s *priv,
                          FAR void *hdrbuf);
static int accept_request(int fd, FAR struct nrc7292_s *priv,
                          FAR void *hdrbuf);
static int shutdown_request(int fd, FAR struct nrc7292_s *priv,
                            FAR void *hdrbuf);
static void reset_daemon_ctx(FAR struct nrc7292_s *priv);
static void reset_nrc7292_config(FAR struct nrc7292_s *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct usrsock_req_handler_s
{
  uint32_t hdrlen;
  int (CODE *fn)(int fd, FAR struct nrc7292_s *priv, FAR void *req);
}
handlers[USRSOCK_REQUEST__MAX] =
{
{
  sizeof(struct usrsock_request_socket_s),
  socket_request,
},
{
  sizeof(struct usrsock_request_close_s),
  close_request,
},
{
  sizeof(struct usrsock_request_connect_s),
  connect_request,
},
{
  sizeof(struct usrsock_request_sendto_s),
  sendto_request,
},
{
  sizeof(struct usrsock_request_recvfrom_s),
  recvfrom_request,
},
{
  sizeof(struct usrsock_request_setsockopt_s),
  setsockopt_request,
},
{
  sizeof(struct usrsock_request_getsockopt_s),
  getsockopt_request,
},
{
  sizeof(struct usrsock_request_getsockname_s),
  getsockname_request,
},
{
  sizeof(struct usrsock_request_getpeername_s),
  getpeername_request,
},
{
  sizeof(struct usrsock_request_bind_s),
  bind_request,
},
{
  sizeof(struct usrsock_request_listen_s),
  listen_request,
},
{
  sizeof(struct usrsock_request_accept_s),
  accept_request,
},
{
  sizeof(struct usrsock_request_ioctl_s),
  ioctl_request,
},
{
  sizeof(struct  usrsock_request_shutdown_s),
  shutdown_request,
},
};

static FAR const struct setup_cmd_s *g_atcmd_list;
static FAR struct nrc7292_s *_daemon;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: read_req
 ****************************************************************************/

static ssize_t
read_req(int fd, FAR const struct usrsock_request_common_s *com_hdr,
         FAR void *req, size_t reqsize)
{
  ssize_t rlen;

  rlen = read(fd, (uint8_t *)req + sizeof(*com_hdr),
              reqsize - sizeof(*com_hdr));

  if (rlen < 0)
    {
      return -errno;
    }

  if (rlen + sizeof(*com_hdr) != reqsize)
    {
      return -EMSGSIZE;
    }

  return rlen;
}

/****************************************************************************
 * Name: usrsock_handle_request
 ****************************************************************************/

static int usrsock_handle_request(int fd, FAR struct nrc7292_s *priv)
{
  FAR struct usrsock_request_common_s *com_hdr;
  union usrsock_request_u req;
  ssize_t rlen;

  com_hdr = (FAR void *)&req;
  rlen = read(fd, com_hdr, sizeof(*com_hdr));

  if (rlen < 0)
    {
      return -errno;
    }

  if (rlen != sizeof(*com_hdr))
    {
      return -EMSGSIZE;
    }

  if (com_hdr->reqid >= USRSOCK_REQUEST__MAX ||
      !handlers[com_hdr->reqid].fn)
    {
      ASSERT(false);
      return -EIO;
    }

  assert(handlers[com_hdr->reqid].hdrlen <= sizeof(req));

  rlen = read_req(fd, com_hdr, &req,
                  handlers[com_hdr->reqid].hdrlen);
  if (rlen < 0)
    {
      return rlen;
    }

  return handlers[com_hdr->reqid].fn(fd, priv, &req);
}

/****************************************************************************
 * Name: set_dhcp_atcmd
 ****************************************************************************/

static void set_dhcp_atcmd(FAR struct nrc7292_s *priv)
{
#ifdef CONFIG_WIRELESS_NRC7292_HALOW_STA_DHCPCLIENT
  g_atcmd_list = get_atcmd_list(ATCMD_DHCP_START);
  priv->exec_mode = ATCMD_MODE;
#endif
}

/****************************************************************************
 * Name: set_socket_close_atcmd
 ****************************************************************************/

static void set_socket_close_atcmd(FAR struct nrc7292_s *priv)
{
  clear_driver_ipaddr(priv);
  g_atcmd_list = get_atcmd_list(ATCMD_ALL_SOCKET_CLOSE);
  priv->exec_mode = ATCMD_MODE;
}

/****************************************************************************
 * Name: monitor_event_handle_request
 ****************************************************************************/

static int monitor_event_handle_request(int fd, FAR struct nrc7292_s *priv)
{
  ssize_t rlen;
  struct monitor_args_s args;

  rlen = read(fd, &args, sizeof(args));

  if (rlen < 0)
    {
      return -errno;
    }

  switch (args.event)
    {
    case MONITOR_DHCP_START:
      set_dhcp_atcmd(priv);
      break;
    case MONITOR_SOCKET_CLOSE:
      set_socket_close_atcmd(priv);
      break;
    case MONITOR_RESET_CTX:
      reset_daemon_ctx(priv);
      break;
    default:
      break;
    }

  return OK;
}

/****************************************************************************
 * Name: socket_request
 ****************************************************************************/

static int socket_request(int fd, FAR struct nrc7292_s *priv,
                          FAR void *hdrbuf)
{
  FAR struct usrsock_request_socket_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: start type=%d\n",
                 __func__, req->type);

  ret = handle_socket_request(fd, priv, req);

  nrc7292_printf("%s: end\n", __func__);
  return ret;
}

/****************************************************************************
 * Name: close_request
 ****************************************************************************/

static int close_request(int fd, FAR struct nrc7292_s *priv,
                         FAR void *hdrbuf)
{
  FAR struct usrsock_request_close_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: start\n", __func__);

  ret = handle_close_request(fd, priv, req);

  nrc7292_printf("%s: end\n", __func__);
  return ret;
}

/****************************************************************************
 * Name: connect_request
 ****************************************************************************/

static int connect_request(int fd, FAR struct nrc7292_s *priv,
                           FAR void *hdrbuf)
{
  FAR struct usrsock_request_connect_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: start\n", __func__);

  ret = handle_connect_request(fd, priv, req);

  nrc7292_printf("%s: end (%d)\n", __func__, ret);
  return ret;
}

/****************************************************************************
 * Name: sendto_request
 ****************************************************************************/

static int sendto_request(int fd, FAR struct nrc7292_s *priv,
                          FAR void *hdrbuf)
{
  FAR struct usrsock_request_sendto_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: start (buflen=%ld)\n",
                 __func__, req->buflen);

  ret = handle_sendto_request(fd, priv, req);

  nrc7292_printf("%s: end\n", __func__);
  return ret;
}

/****************************************************************************
 * Name: recvfrom_request
 ****************************************************************************/

static int recvfrom_request(int fd, FAR struct nrc7292_s *priv,
                            FAR void *hdrbuf)
{
  FAR struct usrsock_request_recvfrom_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: start (req->max_buflen=%ld)\n",
                 __func__, req->max_buflen);

  ret = handle_recvfrom_request(fd, priv, req);

  nrc7292_printf("%s: *** end ret=%d\n", __func__, ret);
  return ret;
}

/****************************************************************************
 * Name: bind_request
 ****************************************************************************/

static int bind_request(int fd, FAR struct nrc7292_s *priv,
                        FAR void *hdrbuf)
{
  FAR struct usrsock_request_bind_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: called ****\n", __func__);

  ret = handle_bind_request(fd, priv, req);

  nrc7292_printf("%s: end (%d)\n", __func__, ret);
  return ret;
}

/****************************************************************************
 * Name: listen_request
 ****************************************************************************/

static int listen_request(int fd, FAR struct nrc7292_s *priv,
                          FAR void *hdrbuf)
{
  FAR struct usrsock_request_listen_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: called ****\n", __func__);

  ret = handle_listen_request(fd, priv, req);

  nrc7292_printf("%s: end (%d)\n", __func__, ret);
  return ret;
}

/****************************************************************************
 * Name: accept_request
 ****************************************************************************/

static int accept_request(int fd, FAR struct nrc7292_s *priv,
                          FAR void *hdrbuf)
{
  FAR struct usrsock_request_accept_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: called ****\n", __func__);

  ret = handle_accept_request(fd, priv, req);

  nrc7292_printf("%s: end\n", __func__);
  return ret;
}

/****************************************************************************
 * Name: setsockopt_request
 ****************************************************************************/

static int setsockopt_request(int fd, FAR struct nrc7292_s *priv,
                              FAR void *hdrbuf)
{
  FAR struct usrsock_request_setsockopt_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: called ****\n", __func__);

  ret = handle_setsockopt_request(fd, priv, req);

  nrc7292_printf("%s: end (ret=%d)\n", __func__, ret);
  return ret;
}

/****************************************************************************
 * Name: getsockopt_request
 ****************************************************************************/

static int getsockopt_request(int fd, FAR struct nrc7292_s *priv,
                              FAR void *hdrbuf)
{
  FAR struct usrsock_request_getsockopt_s *req
    = (FAR struct usrsock_request_getsockopt_s *) hdrbuf;
  int ret;

  nrc7292_printf("%s: called ****\n", __func__);

  ret = handle_getsockopt_request(fd, priv, req);

  nrc7292_printf("%s: end (ret=%d) \n", __func__, ret);
  return ret;
}

/****************************************************************************
 * Name: getsockname_request
 ****************************************************************************/

static int getsockname_request(int fd, FAR struct nrc7292_s *priv,
                               FAR void *hdrbuf)
{
  FAR struct usrsock_request_getsockname_s *req
    = (FAR struct usrsock_request_getsockname_s *) hdrbuf;
  int ret;

  nrc7292_printf("%s: called ****\n", __func__);

  ret = handle_getsockname_request(fd, priv, req);

  nrc7292_printf("%s: end\n", __func__);
  return ret;
}

/****************************************************************************
 * Name: getpeername_request
 ****************************************************************************/

static int getpeername_request(int fd, FAR struct nrc7292_s *priv,
                               FAR void *hdrbuf)
{
  FAR struct usrsock_request_getpeername_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: called ****\n", __func__);

  ret = handle_getpeername_request(fd, priv, req);

  nrc7292_printf("%s: end\n", __func__);
  return ret;
}

/****************************************************************************
 * Name: ioctl_request
 ****************************************************************************/

static int ioctl_request(int fd, FAR struct nrc7292_s *priv,
                         FAR void *hdrbuf)
{
  FAR struct usrsock_request_ioctl_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: called ****\n", __func__);

  ret = handle_ioctl_request(fd, priv, req);

  nrc7292_printf("%s: end\n", __func__);
  return ret;
}

/****************************************************************************
 * Name: shutdown_request
 ****************************************************************************/

static int shutdown_request(int fd, FAR struct nrc7292_s *priv,
                            FAR void *hdrbuf)
{
  FAR struct usrsock_request_shutdown_s *req = hdrbuf;
  int ret;

  nrc7292_printf("%s: called ****\n", __func__);

  ret = handle_shutdown_request(fd, priv, req);

  nrc7292_printf("%s: end\n", __func__);
  return ret;
}

/****************************************************************************
 * Name: notify_sta_conn_event
 ****************************************************************************/

static void notify_sta_conn_event(FAR struct nrc7292_s *priv,
                                   enum wapi_event_id_e event,
                                   FAR char *sta_mac)
{
  struct wapi_event_cb_args_s arg;
  FAR uint8_t *mac = &arg.u.attach.mac[0];

  memset(&arg, 0, sizeof(arg));
  arg.event = event;
  sscanf(sta_mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
         &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

  write(priv->fifotx_fd, &arg, sizeof(arg));
}

/****************************************************************************
 * Name: notify_ip_updated_event
 ****************************************************************************/

static void notify_ip_updated_event(FAR struct nrc7292_s *priv,
                                    FAR char *ipaddr, FAR char *netmask,
                                    FAR char *draddr)
{
  priv->ifconfig.ipaddr.s_addr = inet_addr(ipaddr);
  priv->ifconfig.netmask.s_addr = inet_addr(netmask);
  priv->ifconfig.draddr.s_addr = inet_addr(draddr);

  set_ipinfo_to_driver(priv, true);
}

/****************************************************************************
 * Name: notify_ipaddr_updated_event
 ****************************************************************************/

static void notify_module_crushed_event(FAR struct nrc7292_s *priv)
{
  struct wapi_event_cb_args_s arg;

  if (!priv->suppress_crushed_event)
    {
      memset(&arg, 0, sizeof(arg));
      arg.event = WAPI_EVENT_MODULE_CRUSHED;

      write(priv->fifotx_fd, &arg, sizeof(arg));
    }
  else
    {
      priv->suppress_crushed_event = false;
    }
}

/****************************************************************************
 * Name: reset_daemon_ctx
 ****************************************************************************/

static void reset_daemon_ctx(FAR struct nrc7292_s *priv)
{
  if (priv->atcmd_ctx.is_active)
    {
      send_usock_ack(priv, -EFAULT);
    }

  update_wireless_state(priv, WIRELESS_DISCONNECTED);
  invalidate_all_sockets(priv);
  reset_nrc7292_config(priv);
}

/****************************************************************************
 * Name: parse_atcmd_wireless_event
 ****************************************************************************/

static void parse_atcmd_wireless_event(FAR struct nrc7292_s *priv,
                                      FAR struct atcmd_resp_tokens_s *resp)
{
  if (strncmp(resp->tokens[0], "DISCONNECT", 10) == 0)
    {
      if (resp->count == 4)
        {
          update_wireless_state(priv, WIRELESS_DISCONNECTED);
        }
    }
  else if (strncmp(resp->tokens[0], "CONNECT_SUCCESS", 14) == 0)
    {
      if (resp->count == 4)
        {
          sscanf(resp->tokens[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                 &priv->bssid[0], &priv->bssid[1], &priv->bssid[2],
                 &priv->bssid[3], &priv->bssid[4], &priv->bssid[5]);
          update_wireless_state(priv, WIRELESS_CONNECTED);
        }
    }
  else if (strncmp(resp->tokens[0], "STA_CONNECT", 11) == 0)
    {
      if (resp->count == 2)
        {
          notify_sta_conn_event(priv, WAPI_EVENT_STA_ATTACHED,
                                resp->tokens[1]);
        }
    }
  else if (strncmp(resp->tokens[0], "STA_DISCONNECT", 14) == 0)
    {
      if (resp->count == 2)
        {
          notify_sta_conn_event(priv, WAPI_EVENT_STA_DETACHED,
                                resp->tokens[1]);
        }
    }
  else if (strncmp(resp->tokens[0], "DHCP_SUCCESS", 12) == 0)
    {
      if (resp->count == 4)
        {
          notify_ip_updated_event(priv, resp->tokens[1],
                                  resp->tokens[2], resp->tokens[3]);
        }
    }
}

/****************************************************************************
 * Name: parse_atcmd_socket_event
 ****************************************************************************/

static void parse_atcmd_socket_event(FAR struct nrc7292_s *priv,
                                    FAR struct atcmd_resp_tokens_s *resp)
{
  int socket_id = -1;
  int argc;
  size_t buffered_len;
  FAR struct socket_info_s *sinfo;
  int ret;
  int value;

  if (resp->count < 2)
    {
      return;
    }

  socket_id = strtol(resp->tokens[1], NULL, 10);
  sinfo = nrc7292_socket_get_by_socketid(priv, socket_id);

  if (strncmp(resp->tokens[0], "CONNECT", 7) == 0)
    {
      argc = resp->count - 2;

      if (0 <= argc)
        {
          update_socket_state(priv, socket_id, CONNECTED,
                              argc, &resp->tokens[2]);
        }
    }
  else if (strncmp(resp->tokens[0], "CLOSE", 5) == 0)
    {
      argc = resp->count - 2;

      if (0 <= argc)
        {
          update_socket_state(priv, socket_id, CLOSED,
                              argc, &resp->tokens[2]);
        }

      send_usock_ack_nodata(priv->usock_fd, sinfo->xid, -EPIPE);
    }
  else if (strncmp(resp->tokens[0], "RECV_READY", 10) == 0)
    {
      buffered_len = strtol(resp->tokens[2], NULL, 10);
      update_buffered_packet_size(priv, socket_id, buffered_len);
    }
  else if (strncmp("SEND_DONE", resp->tokens[0], 9) == 0)
    {
      if (sinfo != NULL)
        {
          send_usock_ack_nodata(priv->usock_fd, sinfo->xid, sinfo->len);
          send_usock_socket_events(priv->usock_fd,
                                   USRSOCK_EVENT_SENDTO_READY,
                                   sinfo->usock_id);
        }
    }
  else if (strncmp("SEND_DROP", resp->tokens[0], 9) == 0)
    {
      if (sinfo != NULL)
        {
          send_usock_ack_nodata(priv->usock_fd, sinfo->xid, 0);
          send_usock_socket_events(priv->usock_fd,
                                   USRSOCK_EVENT_SENDTO_READY,
                                   sinfo->usock_id);
        }
    }
  else if (strncmp("SEND_ERROR", resp->tokens[0], 9) == 0)
    {
      if (sinfo != NULL)
        {
          value = strtol(resp->tokens[2], NULL, 10);
          if (value == 16)
            {
              ret = 0;
            }
          else
            {
              ret = -EFAULT;
            }

          send_usock_ack_nodata(priv->usock_fd, sinfo->xid, ret);
          send_usock_socket_events(priv->usock_fd,
                                   USRSOCK_EVENT_SENDTO_READY,
                                   sinfo->usock_id);
        }
    }
}

/****************************************************************************
 * Name: parse_atcmd_event
 ****************************************************************************/

static int parse_atcmd_event(FAR struct nrc7292_s *priv)
{
  FAR struct atcmd_ctx_s *ctx = &priv->atcmd_ctx;
  FAR char *event;
  FAR char *value;
  size_t buflen;
  int ret;
  struct atcmd_resp_tokens_s resp;

  /* Extract Event */

  buflen = 32;
  event = &ctx->atcmd_rxbuf[0];
  ret = read_atcmd_todelim(priv->drv_fd, event, &buflen, ':');

  if (ret < 0 || ATCMD_BUFLEN < buflen)
    {
      return ERROR;
    }

  /* Get Values */

  value = (FAR char *) &ctx->atcmd_rxbuf[buflen];

  buflen = ATCMD_BUFLEN - buflen;
  ret = read_atcmd(priv->drv_fd, value, &buflen);

  if (ret < 0)
    {
      return ERROR;
    }

  split_atcmd_tokens(value, &resp);

  proc_atcmd_response(priv, &resp, event);

  if (strncmp(event, "WEVENT", 6) == 0)
    {
      parse_atcmd_wireless_event(priv, &resp);
    }
  else if (strncmp(event, "SEVENT", 6) == 0)
    {
      parse_atcmd_socket_event(priv, &resp);
    }
  else if (strncmp(event, "BOOT", 4) == 0)
    {
      notify_module_crushed_event(priv);
    }

  return OK;
}

/****************************************************************************
 * Name: parse_atcmd_return
 ****************************************************************************/

static int parse_atcmd_return(FAR struct nrc7292_s *priv, char first)
{
  int ret;
  int retval;
  size_t len;
  FAR char *buf;

  buf = priv->atcmd_ctx.atcmd_rxbuf;

  buf[0] = first;
  len = ATCMD_BUFLEN - 1;
  ret = read_atcmd(priv->drv_fd, &buf[1], &len);

  if (ret < 0)
    {
      return ERROR;
    }

  if (strncmp(buf, "OK", 2) == 0)
    {
      retval = OK;
    }
  else
    {
      retval = ERROR;
    }

  proc_atcmd_return(priv, retval);

  return OK;
}

/****************************************************************************
 * Name: parse_atcmd_resp
 ****************************************************************************/

static int parse_atcmd_resp(FAR struct nrc7292_s *priv)
{
  char ch;
  int ret;

  while (true)
    {
      ch = (char) read_atcmd_char(priv->drv_fd);

      if (ch == '+')
        {
          ret = parse_atcmd_event(priv);
          break;
        }
      else if (ch == 'O' || ch == 'E')
        {
          ret = parse_atcmd_return(priv, ch);
          break;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: init_nrc7292_s
 ****************************************************************************/

static void init_nrc7292_s(FAR struct nrc7292_s *priv)
{
  int i;

  memset(priv, 0, sizeof(struct nrc7292_s));

  priv->exec_mode = ATCMD_MODE;
  priv->usock_enable = TRUE;
  priv->fifotx_fd = -1;
  priv->usock_fd = -1;
  priv->drv_fd = -1;
  priv->scan_state = SCAN_STOPPED;
  priv->wireless_state = WIRELESS_DISCONNECTED;

  for (i = 0; i < SOCKET_COUNT; i++)
    {
      nrc7292_socket_info_init(&priv->sockets[i]);
    }

  priv->wconfig[STA].conf.ifname = WLAN_IFNAME;
  priv->wconfig[STA].conf.ssid = (FAR const char *) (priv + 1);
  priv->wconfig[STA].conf.bssid
    = priv->wconfig[STA].conf.ssid + SSID_MAX_LEN;
  priv->wconfig[STA].conf.passphrase
    = priv->wconfig[STA].conf.bssid + ETHADDR_LEN;
  priv->wconfig[STA].conf.sta_mode = WAPI_MODE_MANAGED;
  priv->wconfig[STA].conf.freq = DEFAULT_BANDFREQUENCY;
  priv->wconfig[STA].conf.flag = WAPI_FREQ_FIXED;
  priv->wconfig[STA].ext.bandwidth = NRC7282_CHANNEL_BANDWIDTH_1MHz;

  priv->wconfig[AP].conf.ifname = RELAY_IFNAME;
  priv->wconfig[AP].conf.ssid
    = priv->wconfig[STA].conf.passphrase + PASSPHRASE_MAX_LEN;
  priv->wconfig[AP].conf.bssid
    = priv->wconfig[AP].conf.ssid + SSID_MAX_LEN;
  priv->wconfig[AP].conf.passphrase
    = priv->wconfig[AP].conf.bssid + ETHADDR_LEN;
  priv->wconfig[AP].conf.sta_mode = WAPI_MODE_MANAGED;
  priv->wconfig[AP].conf.freq = DEFAULT_BANDFREQUENCY;
  priv->wconfig[AP].conf.flag = WAPI_FREQ_FIXED;
  priv->wconfig[AP].ext.bandwidth = NRC7282_CHANNEL_BANDWIDTH_1MHz;

  priv->to_terminate = false;
  priv->suppress_crushed_event = false;

  reset_nrc7292_config(priv);
}

/****************************************************************************
 * Name: do_atcmd_ping_pong
 ****************************************************************************/

static void do_atcmd_ping_pong(FAR struct nrc7292_s *priv)
{
  struct pollfd fds[1];
  char buf[16];
  size_t len;
  int ret;

  while (true)
    {
      ret = send_atcmd(priv->drv_fd, "AT\r\n");
      ASSERT(ret != OK);

      memset(fds, 0, sizeof(fds));
      memset(buf, 0, sizeof(buf));

      /* Check events from usrsock and nrc7292 */

      fds[0].fd     = priv->drv_fd;
      fds[0].events = POLLIN;

      ret = poll(fds, 1, 1000);

      if (fds[0].revents & POLLIN)
        {
          len = sizeof(buf);
          ret = read_atcmd(priv->drv_fd, buf, &len);

          if (ret == OK && 3 == len &&
              strncmp(buf, "OK", 2) == 0)
            {
              break;
            }
        }
    }
}

/****************************************************************************
 * Name: reset_nrc7292_config
 ****************************************************************************/

static void reset_nrc7292_config(FAR struct nrc7292_s *priv)
{
  g_atcmd_list = get_atcmd_list(ATCMD_MODULE_SETUP);
  priv->exec_mode = ATCMD_MODE;
}

/****************************************************************************
 * Name: init_daemon
 ****************************************************************************/

static int init_daemon(FAR struct nrc7292_s *priv)
{
  int ret = 0;

  init_nrc7292_s(priv);

  ret = mkfifo(NRCD_FIFOTX_PATH, 0666);

  if (ret < 0)
    {
      ret = -ENODEV;
      goto error;
    }

  ret = mkfifo(NRCD_FIFORX_PATH, 0666);

  if (ret < 0)
    {
      ret = -ENODEV;
      goto error;
    }

  priv->fifotx_fd = open(NRCD_FIFOTX_PATH, O_RDWR);

  if (priv->fifotx_fd < 0)
    {
      nrc7292_printf("failed to open %s\n", NRCD_FIFOTX_PATH);
      ret = -ENODEV;
      goto error;
    }

  priv->fiforx_fd = open(NRCD_FIFORX_PATH, O_RDWR);

  if (priv->fiforx_fd < 0)
    {
      nrc7292_printf("failed to open %s\n", NRCD_FIFORX_PATH);
      ret = -ENODEV;
      goto error;
    }

  priv->usock_fd = open("/dev/usrsock", O_RDWR);

  if (priv->usock_fd < 0)
    {
      nrc7292_printf("failed to open /dev/usrsock\n");
      ret = -ENODEV;
      goto error;
    }

  priv->drv_fd = open("/dev/nrc7292", O_RDWR);

  if (priv->drv_fd < 0)
    {
      nrc7292_printf("failed to open /dev/nrc7292\n");
      ret = -ENODEV;
      goto error;
    }

  pthread_mutex_init(&priv->evtcb_list_mtx, NULL);

  return 0;

error:
  unlink(NRCD_FIFOTX_PATH);
  unlink(NRCD_FIFORX_PATH);

  if (0 <= priv->fifotx_fd)
    {
      close(priv->fifotx_fd);
      priv->fifotx_fd = -1;
    }

  if (0 <= priv->fiforx_fd)
    {
      close(priv->fifotx_fd);
      priv->fifotx_fd = -1;
    }

  if (0 <= priv->usock_fd)
    {
      close(priv->usock_fd);
      priv->usock_fd = -1;
    }

  if (0 <= priv->drv_fd)
    {
      close(priv->drv_fd);
      priv->drv_fd = -1;
    }

  return ret;
}

/****************************************************************************
 * Name: deinit_daemon
 ****************************************************************************/

static void deinit_daemon(FAR struct nrc7292_s *priv)
{
  unlink(NRCD_FIFOTX_PATH);

  if (0 <= priv->fifotx_fd)
    {
      close(priv->fifotx_fd);
      priv->fifotx_fd = -1;
    }

  if (0 <= priv->usock_fd)
    {
      close(priv->usock_fd);
      priv->usock_fd = -1;
    }

  if (0 <= priv->drv_fd)
    {
      close(priv->drv_fd);
      priv->drv_fd = -1;
    }

  pthread_mutex_destroy(&priv->evtcb_list_mtx);
}

/****************************************************************************
 * Name: exec_atcmds
 ****************************************************************************/

static inline void exec_atcmds(FAR struct nrc7292_s *priv)
{
  if (!priv->atcmd_ctx.is_active)
    {
      if (g_atcmd_list->cmd == NULL)
        {
          /* change mode after all of ATCMDS are executed */

          priv->exec_mode = USRSOCK_MODE;
          return;
        }

      init_atcmd_ctx(&priv->atcmd_ctx, priv->drv_fd, -1, NULL);
      exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                 g_atcmd_list->event, g_atcmd_list->resp_cb,
                 g_atcmd_list->ret_cb,
                 g_atcmd_list->cmd);
      g_atcmd_list++;
    }
}

/****************************************************************************
 * Name: daemon_main_loop
 ****************************************************************************/

static void daemon_event_loop(FAR struct nrc7292_s *priv)
{
  struct pollfd fds[3];
  int fd[3];
  int ret;
  nfds_t nfds;

  /* wait for ready */

  do_atcmd_ping_pong(priv);

  fd[0] = priv->drv_fd;
  fd[1] = priv->fiforx_fd;
  fd[2] = priv->usock_fd;

  while (true)
    {
      memset(fds, 0, sizeof(fds));
      fds[0].fd     = fd[0];
      fds[0].events = POLLIN;
      nfds = 1;

      if (priv->exec_mode == ATCMD_MODE)
        {
          exec_atcmds(priv);
        }

      if (priv->exec_mode == USRSOCK_MODE)
        {
          fds[1].fd     = fd[1];
          fds[1].events = POLLIN;
          nfds++;

          if (!priv->atcmd_ctx.is_active)
            {
              fds[2].fd     = fd[2];
              fds[2].events = POLLIN;
              nfds++;
            }
        }

      ret = poll(fds, nfds, -1);
      ASSERT(0 < ret);

      if (fds[0].revents & POLLIN)
        {
          nrc7292_printf("=== %s: event from /dev/nrc7292\n",
                         __func__);

          ret = parse_atcmd_resp(priv);

          if (ret != OK)
            {
              nrc7292_printf("parse_atcmd_resp error!\n");
            }
        }

      if (fds[1].revents & POLLIN)
        {
          ret = monitor_event_handle_request(fd[1], priv);
          ASSERT(0 == ret);
        }

      if (fds[2].revents & POLLIN)
        {
          ret = usrsock_handle_request(fd[2], priv);
          ASSERT(0 == ret);
        }

      if (priv->to_terminate)
        {
          break;
        }
    }

  close(fd[2]);
  close(fd[1]);
  close(fd[0]);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  pthread_t evtcb_thread;
  int ret;
  size_t ext_area_size;

  ext_area_size = 2 * (SSID_MAX_LEN + ETHADDR_LEN + PASSPHRASE_MAX_LEN);

  if (_daemon)
    {
      nrc7292_printf("%s is already running!\n", argv[0]);
      return -1;
    }

  _daemon = calloc(sizeof(struct nrc7292_s)
                   + ext_area_size, 1);
  ASSERT(_daemon);

  ret = init_daemon(_daemon);

  if (ret < 0)
    {
      goto error;
    }

  ret = pthread_create(&evtcb_thread, NULL,
                       daemon_event_thread, _daemon);

  if (ret < 0)
    {
      nrc7292_printf("failed to pthread_create\n");
      goto error;
    }

  daemon_event_loop(_daemon);

  ret = pthread_join(evtcb_thread, NULL);

  if (ret < 0)
    {
      nrc7292_printf("failed to pthread_join\n");
    }

  ret = unlink(NRCD_FIFOTX_PATH);

  if (ret < 0)
    {
      nrc7292_printf("failed to unlink txfifo\n");
    }

    ret = unlink(NRCD_FIFORX_PATH);

  if (ret < 0)
    {
      nrc7292_printf("failed to unlink rxfifo\n");
    }

error:
  if (_daemon)
    {
      deinit_daemon(_daemon);
      free(_daemon);
      _daemon = NULL;
    }

  return ret;
}

