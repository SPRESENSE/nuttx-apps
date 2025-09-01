/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_socket.c
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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <nuttx/net/usrsock.h>
#include <nuttx/wireless/nrc7292.h>

#include "nrc7292_daemon.h"
#include "nrc7292_socket.h"
#include "nrc7292_ioctl.h"
#include "nrc7292_atcmd.h"
#include "nrc7292_usockif.h"
#include "nrc7292_state.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NRC7292_EBUSY 16
#define ERRCODE_ATREPERROR -1 /* TODO: to be decided */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nrc7292_socket_alloc
 ****************************************************************************/

void nrc7292_socket_info_init(FAR struct socket_info_s *sinfo)
{
  memset(sinfo, 0, sizeof(*sinfo));
  sinfo->socket_id = -1;
  sinfo->usock_id = -1;
  sinfo->state = CLOSED;
}

/****************************************************************************
 * Name: nrc7292_socket_alloc
 ****************************************************************************/

int16_t nrc7292_socket_alloc(FAR struct nrc7292_s *priv,
                             int domain, int type)
{
  FAR struct socket_info_s *sinfo;
  int16_t i;

  for (i = 0; i < SOCKET_COUNT; i++)
    {
      sinfo = &priv->sockets[i];

      if (CLOSED == sinfo->state)
        {
          memset(sinfo, 0, sizeof(*sinfo));
          sinfo->usock_id = i;
          sinfo->socket_id = -1;
          sinfo->state = DISCONNECTED;
          sinfo->bind_state = UNBOUND;
          sinfo->domain = domain;
          sinfo->type = type;
          return i;
        }
    }

  return -1;
}

/****************************************************************************
 * Name: nrc7292_socket_get
 ****************************************************************************/

FAR struct socket_info_s *nrc7292_socket_get(FAR struct nrc7292_s *priv,
                                             int usockid)
{
  FAR struct socket_info_s *sinfo = NULL;

  if (SOCKET_COUNT <= usockid)
    {
      return NULL;
    }

  sinfo = &priv->sockets[usockid];

  if (sinfo->state != CLOSED)
    {
      return sinfo;
    }
  else
    {
      return NULL;
    }
}

/****************************************************************************
 * Name: nrc7292_socket_get_by_sockid
 ****************************************************************************/

FAR struct socket_info_s *
nrc7292_socket_get_by_socketid(FAR struct nrc7292_s *priv,
                               int socket_id)
{
  FAR struct socket_info_s *sinfo;
  int16_t i;

  for (i = 0; i < SOCKET_COUNT; i++)
    {
      sinfo = &priv->sockets[i];

      if (sinfo->socket_id == socket_id &&
          sinfo->state != CLOSED)
        {
          return sinfo;
        }
    }

  return NULL;
}

/****************************************************************************
 * Name: nrc7292_socket_get_by_localport
 ****************************************************************************/

FAR struct socket_info_s *
nrc7292_socket_get_by_localport(FAR struct nrc7292_s *priv,
                                uint16_t lport)
{
  FAR struct socket_info_s *sinfo;
  int16_t i;

  for (i = 0; i < SOCKET_COUNT; i++)
    {
      sinfo = &priv->sockets[i];

      if (sinfo->state != CLOSED &&
          sinfo->bind_addr.sin_port != 0 &&
          (lport == htons(sinfo->bind_addr.sin_port)))
        {
          return sinfo;
        }
    }

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void update_buffered_packet_size(FAR struct nrc7292_s *priv, int socket_id,
                                 size_t buffered_len)
{
  FAR struct socket_info_s *sinfo;
  int16_t events;

  if (0 <= socket_id && socket_id < SOCKET_COUNT)
    {
      priv->buffered_size_table[socket_id] = buffered_len;
    }
  else
    {
      return;
    }

  sinfo = nrc7292_socket_get_by_socketid(priv, socket_id);

  if (sinfo == NULL)
    {
      return;
    }

  if (0 <= sinfo->usock_id && 0 < buffered_len)
    {
      events = USRSOCK_EVENT_RECVFROM_AVAIL;
      send_usock_socket_events(priv->usock_fd, events, sinfo->usock_id);
    }
}

/****************************************************************************
 * Name: get_buffered_packet_size
 ****************************************************************************/

uint32_t get_buffered_packet_size(FAR struct nrc7292_s *priv, int socket_id)
{
  uint32_t size;

  if (0 <= socket_id && socket_id < SOCKET_COUNT)
    {
      size = priv->buffered_size_table[socket_id];
    }
  else
    {
      size = 0;
    }

  return size;
}

/****************************************************************************
 * Name: nrc7292_socket_free
 ****************************************************************************/

int nrc7292_socket_free(FAR struct nrc7292_s *priv,
                        FAR struct socket_info_s *sinfo)
{
  if (sinfo != NULL)
    {
      update_socket_state(priv, sinfo->socket_id, CLOSED, 0, NULL);
      update_buffered_packet_size(priv, sinfo->socket_id, 0);
      sinfo->socket_id = -1;
      sinfo->usock_id = -1;
    }

  return OK;
}

/****************************************************************************
 * Name: nrc7292_socket_free_all
 ****************************************************************************/

int nrc7292_socket_free_all(FAR struct nrc7292_s *priv)
{
  int i;

  for (i = 0; i < SOCKET_COUNT; i++)
    {
      nrc7292_socket_free(priv, &priv->sockets[i]);
    }

  return OK;
}

/****************************************************************************
 * Name: invalidate_all_sockets
 ****************************************************************************/

void invalidate_all_sockets(FAR struct nrc7292_s *priv)
{
  int ret;

  nrc7292_socket_free_all(priv);
  clear_driver_ipaddr(priv);

  init_atcmd_ctx(&priv->atcmd_ctx, -1, -1, NULL);
  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   NULL, NULL,
                   procret_none,
                   "AT+SCLOSE\r\n");
  if (ret < 0)
    {
      nrc7292_printf("failed to send atcmd: %d\n", ret);
    }
}

/****************************************************************************
 * Name: handle_socket_request
 ****************************************************************************/

int handle_socket_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_socket_s *req)
{
  int16_t usockid;
  uint16_t events = 0;
  int ret;

  if (req->domain != AF_INET)
    {
      usockid = -EAFNOSUPPORT;
    }
  else if (!priv->usock_enable && req->domain == AF_INET &&
           req->type != SOCK_CTRL)
    {
      /* If domain is AF_INET while usock_enable is false,
       * set usockid to -ENOTSUP to fallback kernel
       * network stack.
       */

      usockid = -ENOTSUP;
    }
  else
    {
      /* Allocate socket. */

      usockid = nrc7292_socket_alloc(priv, req->domain, req->type);

      if (usockid < 0)
        {
          usockid = -EMFILE;
        }
    }

  /* Send ACK response */

  if (req->type == SOCK_DGRAM)
    {
      events = USRSOCK_EVENT_SENDTO_READY;
    }

  ret = send_usock_ack_common(fd, events, 0, req->head.xid, usockid);

  return ret;
}

/****************************************************************************
 * Name: procret_close
 ****************************************************************************/

static int procret_close(FAR struct nrc7292_s *priv, int ret)
{
  int retval;

  if (ret == OK)
    {
      nrc7292_socket_free(priv, priv->atcmd_ctx.sinfo);
      retval = OK;
    }
  else
    {
      retval = -EBADFD;
    }

  send_usock_ack(priv, retval);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: handle_close_request
 ****************************************************************************/

int handle_close_request(int fd, FAR struct nrc7292_s *priv,
                         FAR struct usrsock_request_close_s *req)
{
  FAR struct socket_info_s *sinfo;
  int ret;

  sinfo = nrc7292_socket_get(priv, req->usockid);

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  if (sinfo == NULL)
    {
      ret = OK;
    }
  else
    {
      if (sinfo->socket_id < 0)
        {
          nrc7292_socket_free(priv, sinfo);
          ret = OK;
        }
      else
        {
          ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                           NULL, NULL,
                           procret_close,
                           "AT+SCLOSE=%d\r\n", sinfo->socket_id);
          if (ret == 0)
            {
              goto skip_ack;
            }
        }
    }

  return send_usock_ack_nodata(fd, req->head.xid, ret);

skip_ack:
  return OK;
}

/****************************************************************************
 * Name: handle_shutdown_request
 ****************************************************************************/

int handle_shutdown_request(int fd, FAR struct nrc7292_s *priv,
                            FAR struct usrsock_request_shutdown_s *req)
{
  return send_usock_ack_nodata(fd, req->head.xid, -ENOTSUP);
}

/****************************************************************************
 * Name: procresp_connect
 ****************************************************************************/

static int procresp_connect(FAR struct nrc7292_s *priv,
                            FAR struct atcmd_resp_tokens_s *resp)
{
  if (0 < resp->count)
    {
      priv->atcmd_ctx.sinfo->socket_id = strtol(resp->tokens[0], NULL, 10);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procret_setoptions
 ****************************************************************************/

static int procret_setoptions(FAR struct nrc7292_s *priv, int ret)
{
  FAR struct socket_info_s *sinfo;
  int16_t events;
  int atcmd_ret;

  sinfo = priv->atcmd_ctx.sinfo;

  if (sinfo->sopt_flags & (1 << SO_KEEPALIVE))
    {
      exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                 NULL, NULL,
                 procret_setoptions,
                 "AT+STCPKEEPALIVE=%d,%d\r\n",
                 sinfo->socket_id, sinfo->so_keepalive);
      sinfo->sopt_flags &= ~(1 << SO_KEEPALIVE);

      atcmd_ret = ATCMD_CONTINUE;
    }
  else if (sinfo->sopt_flags & (1 << TCP_NODELAY))
    {
      exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                 NULL, NULL,
                 procret_setoptions,
                 "AT+STCPNODELAY=%d,%d\r\n",
                 sinfo->socket_id, sinfo->tcp_nodelay);
      sinfo->sopt_flags &= ~(1 << TCP_NODELAY);

      atcmd_ret = ATCMD_CONTINUE;
    }
  else
    {
      send_usock_ack(priv, OK);

      events = USRSOCK_EVENT_CONNECTED | USRSOCK_EVENT_SENDTO_READY;
      send_usock_socket_events(priv->atcmd_ctx.fd, events, sinfo->usock_id);

      atcmd_ret = ATCMD_FIN;
    }

  return atcmd_ret;
}

/****************************************************************************
 * Name: procret_connect
 ****************************************************************************/

static int procret_connect(FAR struct nrc7292_s *priv, int ret)
{
  FAR struct socket_info_s *sinfo;
  int atcmd_ret;

  sinfo = priv->atcmd_ctx.sinfo;

  if (ret == OK)
    {
      update_socket_state(priv, sinfo->socket_id, CONNECTED, 0, NULL);

      atcmd_ret = procret_setoptions(priv, ret);
    }
  else
    {
      send_usock_ack(priv, -ECONNREFUSED);
      atcmd_ret = ATCMD_FIN;
    }

  return atcmd_ret;
}

/****************************************************************************
 * Name: handle_connect_request
 ****************************************************************************/

int handle_connect_request(int fd, FAR struct nrc7292_s *priv,
                           FAR struct usrsock_request_connect_s *req)
{
  FAR struct socket_info_s *sinfo;
  struct sockaddr_in addr;
  ssize_t rlen;
  int ret;

  /* Check if address size ok. */

  if (req->addrlen > sizeof(addr))
    {
      ret = -EFAULT;
      goto error;
    }

  /* Read address. */

  rlen = read(fd, &addr, sizeof(addr));

  if (rlen < 0 || rlen < req->addrlen)
    {
      ret = -EFAULT;
      goto error;
    }

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  if (sinfo->domain != AF_INET)
    {
      ret = -EAFNOSUPPORT;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  if (sinfo->type == SOCK_STREAM)
    {
      sinfo->remote_addr = addr;
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       "SOPEN", procresp_connect,
                       procret_connect,
                       "AT+SOPEN=\"TCP\",\"%s\",%d,%d\r\n",
                       inet_ntoa(addr.sin_addr), htons(addr.sin_port),
                       sinfo->so_reuseaddr);
      if (ret < 0)
        {
          goto error;
        }
    }
  else if (sinfo->type == SOCK_DGRAM)
    {
      sinfo->remote_addr = addr;
      update_socket_state(priv, sinfo->socket_id, CONNECTED, 0, NULL);
      ret = OK;
      goto error;
    }
  else
    {
      ret = -ESOCKTNOSUPPORT;
      goto error;
    }

  return ret;

error:
  return send_usock_ack_nodata(fd, req->head.xid, ret);
}

/****************************************************************************
 * Name: procresp_sopen
 ****************************************************************************/

static int procresp_sopen(FAR struct nrc7292_s *priv,
                          FAR struct atcmd_resp_tokens_s *resp)
{
  if (0 < resp->count)
    {
      priv->atcmd_ctx.sinfo->socket_id = strtol(resp->tokens[0], NULL, 10);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: handle_bind_request
 ****************************************************************************/

int handle_bind_request(int fd, FAR struct nrc7292_s *priv,
                        FAR struct usrsock_request_bind_s *req)
{
  FAR struct socket_info_s *sinfo;
  struct sockaddr_in addr;
  ssize_t rlen;
  int ret = OK;

  /* Check if address size ok. */

  if (req->addrlen > sizeof(addr))
    {
      ret = -EFAULT;
      goto error;
    }

  /* Read address. */

  rlen = read(fd, &addr, sizeof(addr));

  if (rlen < 0 || rlen < req->addrlen)
    {
      ret = -EFAULT;
      goto error;
    }

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  if (sinfo->domain != AF_INET)
    {
      ret = -EAFNOSUPPORT;
      goto error;
    }

  if (sinfo->bind_state == UNBOUND)
    {
      sinfo->bind_addr = addr;
      update_bind_state(sinfo, BOUND);
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  if (sinfo->type == SOCK_DGRAM)
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       "SOPEN", procresp_sopen,
                       send_usock_ack_fin,
                       "AT+SOPEN=\"UDP\",%d,%d\r\n",
                       htons(sinfo->bind_addr.sin_port),
                       sinfo->so_reuseaddr);
    }
  else if (sinfo->type == SOCK_STREAM)
    {
      ret = OK;
      send_usock_ack_nodata(fd, req->head.xid, OK);
    }
  else
    {
      ret = -EINVAL;
    }

error:
  if (ret < 0)
    {
      send_usock_ack_nodata(fd, req->head.xid, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: procret_listen
 ****************************************************************************/

static int procret_listen(FAR struct nrc7292_s *priv, int ret)
{
  FAR struct socket_info_s *sinfo;

  sinfo = priv->atcmd_ctx.sinfo;

  if (ret == OK)
    {
      update_socket_state(priv, sinfo->socket_id, LISTEN, 0, NULL);
      send_usock_ack(priv, ret);
    }
  else
    {
      send_usock_ack(priv, ret);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: handle_listen_request
 ****************************************************************************/

int handle_listen_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_listen_s *req)
{
  FAR struct socket_info_s *sinfo;
  int ret = OK;

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  if (sinfo->domain != AF_INET)
    {
      ret = -EAFNOSUPPORT;
      goto error;
    }

  if (sinfo->bind_state == UNBOUND)
    {
      ret = -EINVAL;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  if (sinfo->type == SOCK_STREAM)
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       "SOPEN", procresp_sopen,
                       procret_listen,
                       "AT+SOPEN=\"TCP\",%d,%d\r\n",
                       htons(sinfo->bind_addr.sin_port),
                       sinfo->so_reuseaddr);
    }
  else
    {
      ret = -EOPNOTSUPP;
    }

error:
  if (ret < 0)
    {
      send_usock_ack_nodata(fd, req->head.xid, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: procret_accept
 ****************************************************************************/

static int procret_accept(FAR struct nrc7292_s *priv, int ret)
{
  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procresp_accept
 ****************************************************************************/

static int procresp_accept(FAR struct nrc7292_s *priv,
                           FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct socket_info_s *sinfo;
  int socket_id = -1;
  int argc = 0;
  int ret;

  if (resp->count < 5)
    {
      ret = -EINVAL;
      goto error;
    }

  socket_id = strtol(resp->tokens[0], NULL, 10);
  sinfo = nrc7292_socket_get_by_socketid(priv, socket_id);

  if (sinfo != NULL)
    {
      /* usock is already allocated */

      goto skip_resp;
    }

  if (strncmp(resp->tokens[1], "TCP", 3) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  argc = resp->count - 2;

  if (argc == 3)
    {
      update_socket_state(priv, socket_id, CONNECTED,
                          argc, &resp->tokens[2]);
    }
  else
    {
      ret = -EINVAL;
      goto error;
    }

  return ATCMD_FIN;

error:
  send_usock_ack(priv, ret);

skip_resp:
  return ATCMD_FIN;
}

/****************************************************************************
 * Name: handle_accept_request
 ****************************************************************************/

int handle_accept_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_accept_s *req)
{
  FAR struct socket_info_s *sinfo;
  int ret = OK;

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  if (sinfo->state != LISTEN)
    {
      ret = -EINVAL;
    }
  else
    {
      update_socket_state(priv, sinfo->socket_id, ACCEPTING, 0, NULL);

      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                        "SLIST", procresp_accept,
                        procret_accept,
                        "AT+SLIST?\r\n");
    }

error:
  if (ret < 0)
    {
      send_usock_ack_nodata(fd, req->head.xid, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: handle_setsockopt_solsocket_request
 ****************************************************************************/

static int handle_setsockopt_solsocket_request(FAR struct nrc7292_s *priv,
                                           int16_t option,
                                           FAR struct socket_info_s *sinfo,
                                           FAR void *value)
{
  int ret = OK;

  switch (option)
    {
    case SO_KEEPALIVE:
      {
        sinfo->sopt_flags |= (1 << SO_KEEPALIVE);
        sinfo->so_keepalive = *((int *) value);

        if (0 <= sinfo->socket_id)
          {
            ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                             NULL, NULL,
                             send_usock_ack_fin,
                             "AT+STCPKEEPALIVE=%d,%d\r\n",
                             sinfo->socket_id, sinfo->so_keepalive);

            if (ret == OK)
              {
                goto skip_ack;
              }
            else
              {
                ret = -EFAULT;
              }
          }
      }
      break;

    case SO_REUSEADDR:
      {
        sinfo->sopt_flags |= (1 << SO_REUSEADDR);
        sinfo->so_reuseaddr = (*((int *) value) != 0) ? 1 : 0;
        ret = OK;
      }
      break;

    default:
      ret = -ENOPROTOOPT;
      break;
    }

  send_usock_ack(priv, ret);
  return ret;

skip_ack:
  return ret;
}

/****************************************************************************
 * Name: handle_setsockopt_tcp_request
 ****************************************************************************/

static int handle_setsockopt_tcp_request(FAR struct nrc7292_s *priv,
                                         int16_t option,
                                         FAR struct socket_info_s *sinfo,
                                         FAR void *value)
{
  int ret = OK;

  switch (option)
    {
    case TCP_NODELAY:
      {
        sinfo->sopt_flags |= (1 << TCP_NODELAY);
        sinfo->tcp_nodelay = *((int *) value);

        if (0 <= sinfo->socket_id)
          {
            ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                             NULL, NULL,
                             send_usock_ack_fin,
                             "AT+STCPNODELAY=%d,%d\r\n",
                             sinfo->socket_id, sinfo->tcp_nodelay);

            if (ret == OK)
              {
                goto skip_ack;
              }
            else
              {
                ret = -EFAULT;
              }
          }
      }
      break;

    default:
      ret = -ENOPROTOOPT;
      break;
    }

  send_usock_ack(priv, ret);
  return ret;

skip_ack:
  return ret;
}

/****************************************************************************
 * Name: handle_setsockopt_request
 ****************************************************************************/

int handle_setsockopt_request(int fd, FAR struct nrc7292_s *priv,
                              FAR struct usrsock_request_setsockopt_s *req)
{
  FAR struct socket_info_s *sinfo;
  ssize_t rlen;
  int ret;

  if (BUFLEN < req->valuelen)
    {
      ret = -E2BIG;
      goto error;
    }

  rlen = read(fd, priv->buf, req->valuelen);

  if (rlen < 0)
    {
      ret = -EINVAL;
      goto error;
    }

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  switch (req->level)
    {
    case SOL_SOCKET:
      ret = handle_setsockopt_solsocket_request(priv, req->option, sinfo,
                                                priv->buf);
      break;

    case IPPROTO_TCP:
      ret = handle_setsockopt_tcp_request(priv, req->option, sinfo,
                                          priv->buf);
      break;

    default:
      ret = -ENOPROTOOPT;
      goto error;
    }

  return ret;

error:
  if (ret < 0)
    {
      send_usock_ack_nodata(fd, req->head.xid, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: handle_getsockopt_solsocket_request
 ****************************************************************************/

static int handle_getsockopt_solsocket_request(FAR struct nrc7292_s *priv,
                                           int16_t option,
                                           FAR struct socket_info_s *sinfo)
{
  FAR struct atcmd_ctx_s *ctx = &priv->atcmd_ctx;
  FAR void *value = (FAR void *) ctx->resp.value;
  int ret = OK;

  switch (option)
    {
    case SO_KEEPALIVE:
      *((int *) value) = sinfo->so_keepalive;
      ctx->resp_len = sizeof(int);
      break;

    case SO_REUSEADDR:
      *((int *) value) = sinfo->so_reuseaddr;
      ctx->resp_len = sizeof(int);
      break;

    default:
      ret = -ENOPROTOOPT;
      break;
    }

  return ret;
}

/****************************************************************************
 * Name: handle_getsockopt_tcp_request
 ****************************************************************************/

static int handle_getsockopt_tcp_request(FAR struct nrc7292_s *priv,
                                         int16_t option,
                                         FAR struct socket_info_s *sinfo)
{
  FAR struct atcmd_ctx_s *ctx = &priv->atcmd_ctx;
  FAR void *value = (FAR void *) ctx->resp.value;
  int ret = OK;

  switch (option)
    {
    case TCP_NODELAY:
      *((int *) value) = sinfo->tcp_nodelay;
      ctx->resp_len = sizeof(int);
      break;

    default:
      ret = -ENOPROTOOPT;
      break;
    }

  return ret;
}

/****************************************************************************
 * Name: handle_getsockopt_request
 ****************************************************************************/

int handle_getsockopt_request(int fd, FAR struct nrc7292_s *priv,
                              FAR struct usrsock_request_getsockopt_s *req)
{
  FAR struct socket_info_s *sinfo;
  int ret = OK;

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  switch (req->level)
    {
    case SOL_SOCKET:
      ret = handle_getsockopt_solsocket_request(priv, req->option, sinfo);
      break;

    case IPPROTO_TCP:
      ret = handle_getsockopt_tcp_request(priv, req->option, sinfo);
      break;

    default:
      ret = -ENOPROTOOPT;
      break;
    }

error:
  return send_usock_ack_nodata(fd, req->head.xid, ret);
}

/****************************************************************************
 * Name: procret_getsockname
 ****************************************************************************/

static int procret_getsockname(FAR struct nrc7292_s *priv, int ret)
{
  if (ret == OK)
    {
      if (priv->atcmd_ctx.resp_len == 0)
        {
          ret = -EBADFD;
        }
    }

  send_usock_ack(priv, ret);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procresp_getsockname
 ****************************************************************************/

static int procresp_getsockname(FAR struct nrc7292_s *priv,
                                FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct socket_info_s *sinfo;
  FAR struct sockaddr_in *addr;
  int socket_id;
  uint16_t lport;

  sinfo = priv->atcmd_ctx.sinfo;
  addr = (FAR struct sockaddr_in *) priv->atcmd_ctx.resp.value;

  if (resp->count < 5)
    {
      goto error;
    }

  socket_id = strtol(resp->tokens[0], NULL, 10);

  if (socket_id != sinfo->socket_id)
    {
      goto error;
    }

  memset(addr, 0, sizeof(*addr));
  lport = strtoul(resp->tokens[4], NULL, 10);
  addr->sin_port = ntohs(lport);
  addr->sin_family = AF_INET;
  addr->sin_addr = priv->ifconfig.ipaddr;
  priv->atcmd_ctx.resp_len = sizeof(*addr);

error:
  return ATCMD_FIN;
}

/****************************************************************************
 * Name: handle_getsockname_request
 ****************************************************************************/

int handle_getsockname_request(int fd, FAR struct nrc7292_s *priv,
                               FAR struct usrsock_request_getsockname_s *req)
{
  FAR struct socket_info_s *sinfo;
  int ret = OK;

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  if (sinfo->state != CONNECTED)
    {
      ret = -ENOTCONN;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "SLIST", procresp_getsockname,
                   procret_getsockname,
                   "AT+SLIST?\r\n");

error:
  if (ret < 0)
    {
      send_usock_ack_nodata(fd, req->head.xid, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: procret_getpeername
 ****************************************************************************/

static int procret_getpeername(FAR struct nrc7292_s *priv, int ret)
{
  if (ret == OK)
    {
      if (priv->atcmd_ctx.resp_len == 0)
        {
          ret = -EBADFD;
        }
    }

  send_usock_ack(priv, ret);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procresp_getpeername
 ****************************************************************************/

static int procresp_getpeername(FAR struct nrc7292_s *priv,
                                FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct socket_info_s *sinfo;
  FAR struct sockaddr_in *addr;
  int socket_id;
  uint16_t rport;

  sinfo = priv->atcmd_ctx.sinfo;
  addr = (FAR struct sockaddr_in *) priv->atcmd_ctx.resp.value;

  if (resp->count < 5)
    {
      goto error;
    }

  socket_id = strtol(resp->tokens[0], NULL, 10);

  if (socket_id != sinfo->socket_id)
    {
      goto error;
    }

  memset(addr, 0, sizeof(*addr));
  rport = strtoul(resp->tokens[3], NULL, 10);
  addr->sin_port = ntohs(rport);
  addr->sin_family = AF_INET;
  addr->sin_addr = priv->ifconfig.ipaddr;
  addr->sin_addr.s_addr = inet_addr(resp->tokens[2]);
  priv->atcmd_ctx.resp_len = sizeof(*addr);

error:
  return ATCMD_FIN;
}

/****************************************************************************
 * Name: handle_getpeername_request
 ****************************************************************************/

int handle_getpeername_request(int fd, FAR struct nrc7292_s *priv,
                               FAR struct usrsock_request_getpeername_s *req)
{
  FAR struct socket_info_s *sinfo;
  int ret = OK;

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  if (sinfo->state != CONNECTED)
    {
      ret = -ENOTCONN;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "SLIST", procresp_getpeername,
                   procret_getpeername,
                   "AT+SLIST?\r\n");

error:
  if (ret < 0)
    {
      send_usock_ack_nodata(fd, req->head.xid, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: procret_sendto
 ****************************************************************************/

static int procret_sendto(FAR struct nrc7292_s *priv, int ret)
{
  FAR struct socket_info_s *sinfo;
  int atcmd_ret = ATCMD_FIN;

  if (ret == OK)
    {
      ret = send_atcmd_rawdata(priv->drv_fd, priv->buf, priv->buf_len);

      ret = send_usock_ack_in_progress(priv->atcmd_ctx.fd,
                                   priv->atcmd_ctx.xid, priv->buf_len);
      atcmd_ret = ATCMD_FIN;
    }
  else
    {
      sinfo = priv->atcmd_ctx.sinfo;

      send_usock_ack(priv, ERRCODE_ATREPERROR);
      send_usock_socket_events(priv->atcmd_ctx.fd,
                               USRSOCK_EVENT_SENDTO_READY, sinfo->usock_id);
      atcmd_ret = ATCMD_FIN;
    }

  return atcmd_ret;
}

/****************************************************************************
 * Name: procret_sopen_in_sendto
 ****************************************************************************/

static int procret_sopen_in_sendto(FAR struct nrc7292_s *priv, int ret)
{
  FAR struct atcmd_ctx_s *ctx = &priv->atcmd_ctx;
  int atcmd_ret;

  if (ret == OK)
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       NULL, NULL,
                       procret_sendto,
                       "AT+SSEND=%d,\"%s\",%d,%d,1\r\n",
                       ctx->sinfo->socket_id,
                       inet_ntoa(ctx->dst_addr.sin_addr),
                       htons(ctx->dst_addr.sin_port),
                       priv->buf_len);
      atcmd_ret = ATCMD_CONTINUE;
    }
  else
    {
      send_usock_ack(priv, ret);
      send_usock_socket_events(priv->atcmd_ctx.fd,
                               USRSOCK_EVENT_SENDTO_READY,
                               ctx->sinfo->usock_id);
      atcmd_ret = ATCMD_FIN;
    }

  return atcmd_ret;
}

/****************************************************************************
 * Name: handle_sendto_request
 ****************************************************************************/

int handle_sendto_request(int fd, FAR struct nrc7292_s *priv,
                          FAR struct usrsock_request_sendto_s *req)
{
  FAR struct socket_info_s *sinfo = NULL;
  FAR struct sockaddr_in *addr = NULL;
  uint8_t addrbuf[32];
  ssize_t rlen = 0;
  int ret = OK;

  ASSERT(req->addrlen < sizeof(addrbuf));

  if (req->addrlen > 0)
    {
      rlen = read(fd, addrbuf, req->addrlen);

      if (rlen < 0)
        {
          ret = -EINVAL;
          goto error;
        }

      addr = (FAR struct sockaddr_in *) addrbuf;
    }

  if (req->buflen > 0)
    {
      rlen = read(fd, priv->buf, req->buflen);

      if (rlen < 0)
        {
          ret = -EINVAL;
          goto error;
        }
    }

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  if (0 < rlen)
    {
      priv->buf_len = rlen;
    }
  else
    {
      /* no data to send */

      ret = 0;
      goto error;
    }

  if (SOCK_STREAM == sinfo->type && CONNECTED != sinfo->state)
    {
      ret = -ENOTCONN;
      goto error;
    }

  if (SOCK_STREAM == sinfo->type && addr != NULL)
    {
      ret = -EISCONN;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);
  sinfo->len = priv->buf_len;

  if (sinfo->type == SOCK_DGRAM)
    {
      FAR struct sockaddr_in *dst_addr = NULL;

      if (CONNECTED == sinfo->state)
        {
          dst_addr = &sinfo->remote_addr;
        }
      else
        {
          if (req->addrlen == 0)
            {
              ret = -EINVAL;
              goto error;
            }

          dst_addr = addr;
        }

      if (UDP_PAYLOAD_MAX < priv->buf_len)
        {
          ret = -EMSGSIZE;
          goto error;
        }

      if (sinfo->socket_id < 0)
        {
          uint16_t port;

          if (sinfo->bind_state == UNBOUND)
            {
              /* set port over 49152 */

              port =
                (((uint16_t) sinfo->usock_id) & 0x00ff) | 0xc000;
            }
          else
            {
              port = htons(sinfo->bind_addr.sin_port);
            }

          priv->atcmd_ctx.dst_addr = *dst_addr;
          ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                           "SOPEN", procresp_sopen,
                           procret_sopen_in_sendto,
                           "AT+SOPEN=\"udp\",%d\r\n",
                           port);
        }
      else
        {
          ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                           NULL, NULL,
                           procret_sendto,
                           "AT+SSEND=%d,\"%s\",%d,%d,1\r\n",
                           sinfo->socket_id,
                           inet_ntoa(dst_addr->sin_addr),
                           htons(dst_addr->sin_port),
                           priv->buf_len);
        }

      if (ret < 0)
        {
          goto error;
        }
    }
  else
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       NULL, NULL,
                       procret_sendto,
                       "AT+SSEND=%d,%d,1\r\n",
                       sinfo->socket_id, priv->buf_len);

      if (ret < 0)
        {
          goto error;
        }
    }

  return ret;

error:
  send_usock_ack_nodata(fd, req->head.xid, ret);

  if (sinfo != NULL && 0 <= sinfo->usock_id)
    {
      send_usock_socket_events(fd,
                               USRSOCK_EVENT_SENDTO_READY,
                               sinfo->usock_id);
    }

  return OK;
}

/****************************************************************************
 * Name: procret_recvfrom
 ****************************************************************************/

static int procret_recvfrom(FAR struct nrc7292_s *priv, int ret)
{
  if (ret != OK)
    {
      send_usock_ack(priv, -ENOTCONN);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procresp_recvfrom
 ****************************************************************************/

static int procresp_recvfrom(FAR struct nrc7292_s *priv,
                             FAR struct atcmd_resp_tokens_s *resp)
{
  int socket_id;
  int data_len;
  size_t addr_len;
  ssize_t rem_len;
  ssize_t offset = 0;
  FAR struct socket_info_s *sinfo;
  FAR struct sockaddr_in *remote_addr = &priv->atcmd_ctx.resp.remote_addr;
  int ret = 0;

  if (resp->count < 4)
    {
      ret = -EINVAL;
      goto error;
    }

  sinfo = priv->atcmd_ctx.sinfo;
  socket_id = strtoul(resp->tokens[0], NULL, 10);
  data_len = strtoul(resp->tokens[1], NULL, 10);
  memset(remote_addr, 0, sizeof(*remote_addr));
  remote_addr->sin_family = AF_INET;
  remote_addr->sin_addr.s_addr = inet_addr(resp->tokens[2]);
  remote_addr->sin_port = htons(strtoul(resp->tokens[3], NULL, 10));

  rem_len = data_len;

  while (0 < rem_len)
    {
      ret = read(priv->drv_fd, priv->buf + offset, rem_len);

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

  priv->buf_len = data_len - rem_len;
  addr_len = priv->atcmd_ctx.resp_len;

  ret = send_usock_ack_with_packet(priv->atcmd_ctx.fd,
                                   priv->atcmd_ctx.xid,
                                   data_len,
                                   (FAR uint8_t *) remote_addr,
                                   addr_len,
                                   priv->buf,
                                   addr_len + data_len);

  update_buffered_packet_size(priv, socket_id, 0);

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: handle_recvfrom_request
 ****************************************************************************/

int handle_recvfrom_request(int fd, FAR struct nrc7292_s *priv,
                            FAR struct usrsock_request_recvfrom_s *req)
{
  FAR struct socket_info_s *sinfo;
  int ret = OK;
  size_t len;

  sinfo = nrc7292_socket_get(priv, req->usockid);

  if (sinfo == NULL)
    {
      ret = -EBADFD;
      goto error;
    }

  if (SOCK_STREAM == sinfo->type && CONNECTED != sinfo->state)
    {
      ret = -ENOTCONN;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, sinfo);

  len = get_buffered_packet_size(priv, sinfo->socket_id);

  if (req->max_buflen < len)
    {
      len = req->max_buflen;
    }

  if (0 < req->max_addrlen)
    {
      priv->atcmd_ctx.resp_len =
        MIN(req->max_addrlen, sizeof(priv->atcmd_ctx.resp.remote_addr));
    }

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "RXD", procresp_recvfrom,
                   procret_recvfrom,
                   "AT+SRECV=%d,%d\r\n",
                   sinfo->socket_id, len);

error:
  if (ret < 0)
    {
      send_usock_ack_nodata(fd, req->head.xid, ret);
    }

  return ret;
}

