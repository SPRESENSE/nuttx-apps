/****************************************************************************
 * apps/lte/alt1250/alt1250_socket.h
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

#ifndef __APPS_LTE_ALT1250_ALT1250_SOCKET_H
#define __APPS_LTE_ALT1250_ALT1250_SOCKET_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/net/usrsock.h>
#include <nuttx/net/sms.h>
#include <assert.h>

#include "alt1250_usockif.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SELECT_WRITABLE (1 << 1)
#define SELECT_READABLE (1 << 2)
#define SELECTABLE_MASK (SELECT_WRITABLE | SELECT_READABLE)

#define USOCKET_SET_REQUEST(sock, _reqid, _xid) \
  do \
    { \
      (sock)->request.reqid = (_reqid); \
      (sock)->request.xid = (_xid); \
    } \
  while (0)

#define USOCKET_SET_SOCKTYPE(sock, _domain, _type, _protocol) \
  do \
    { \
      (sock)->domain = (_domain);  \
      (sock)->type = (_type);  \
      (sock)->protocol = (_protocol);  \
    } \
  while (0)

#define USOCKET_SET_REQADDRLEN(sock, _addrlen) \
  do \
    { \
      (sock)->sock_req.addrbuflen.addr.addrlen = (_addrlen); \
    } \
  while (0)

#define USOCKET_SET_REQBACKLOG(sock, _backlog) \
  do \
    { \
      (sock)->sock_req.backlog = (_backlog); \
    } \
  while (0)

#define USOCKET_SET_REQBUFLEN(sock, _buflen) \
  do \
    { \
      (sock)->sock_req.addrbuflen.buflen = (_buflen); \
    } \
  while (0)

#define USOCKET_SET_REQFLAGS(sock, _flags) \
  do \
    { \
      (sock)->sock_req.addrbuflen.flags = (_flags); \
    } \
  while (0)

#define USOCKET_SET_REQSOCKOPT(sock, _level, _opt, _optlen) \
  do \
    { \
      (sock)->sock_req.opt.level = (_level); \
      (sock)->sock_req.opt.option = (_opt); \
      (sock)->sock_req.opt.optlen = (_optlen); \
    } \
  while (0)

#define USOCKET_REQID(sock)       ((sock)->request.reqid)
#define USOCKET_XID(sock)         ((sock)->request.xid)
#define USOCKET_DOMAIN(sock)      ((sock)->domain)
#define USOCKET_TYPE(sock)        ((sock)->type)
#define USOCKET_PROTOCOL(sock)    ((sock)->protocol)
#define USOCKET_REQADDRLEN(sock)  ((sock)->sock_req.addrbuflen.addr.addrlen)
#define USOCKET_REQADDR(sock)     ((sock)->sock_req.addrbuflen.addr.addr)
#define USOCKET_REQBACKLOG(sock)  ((sock)->sock_req.backlog)
#define USOCKET_REQBUFLEN(sock)   ((sock)->sock_req.addrbuflen.buflen)
#define USOCKET_REQFLAGS(sock)    ((sock)->sock_req.addrbuflen.flags)
#define USOCKET_REQOPTLEVEL(sock) ((sock)->sock_req.opt.level)
#define USOCKET_REQOPTOPT(sock)   ((sock)->sock_req.opt.option)
#define USOCKET_REQOPTLEN(sock)   ((sock)->sock_req.opt.optlen)
#define USOCKET_REQOPTVAL(sock)   ((sock)->sock_req.opt.value)
#define USOCKET_STATE(sock)       ((sock)->ctx.state)
#define USOCKET_OLDSTATE(sock)    ((sock)->old_state)
#define USOCKET_ALTSOCKID(sock)   ((sock)->ctx.altsockid)
#define USOCKET_USOCKID(sock)     ((sock)->usockid)
#define USOCKET_REFID(sock)       (&(sock)->refids)

#define USOCKET_REP_RESPONSE(sock) ((sock)->sock_reply.outparams)
#define USOCKET_REP_RESULT(sock)  (&(sock)->sock_reply.rep_result)
#define USOCKET_REP_ERRCODE(sock) (&(sock)->sock_reply.rep_errcode)
#define USOCKET_REP_ADDRLEN(sock) \
  (&(sock)->sock_reply.sock_reply_opt.addr.addrlen)
#define USOCKET_REP_ADDR(sock) \
  (&(sock)->sock_reply.sock_reply_opt.addr.addr)
#define USOCKET_REP_OPTLEN(sock) \
  (&(sock)->sock_reply.sock_reply_opt.opt.optlen)
#define USOCKET_REP_OPTVAL(sock) \
  (&(sock)->sock_reply.sock_reply_opt.opt.value[0])

#define USOCKET_SET_RESPONSE(sock, n, p) \
  do \
    { \
      int iii = (n); \
      if (iii < _OUTPUT_ARG_MAX) \
        { \
          (sock)->sock_reply.outparams[iii] = (p); \
        } \
      else \
        { \
          PANIC(); \
        } \
    } \
  while(0)

#define USOCKET_SET_ALTSOCKID(sock, id) \
  do \
    { \
      (sock)->ctx.altsockid = (id); \
    } \
  while (0)
#define USOCKET_SET_STATE(sock, st) \
  do \
    { \
      (sock)->ctx.state = (st); \
    } \
  while (0)
#define USOCKET_SET_OLDSTATE(sock, st) \
  do \
    { \
      (sock)->old_state = (st); \
    } \
  while (0)
#define USOCKET_SET_SELECTABLE(sock, rw) \
  do \
    { \
      (sock)->select_condition |= (rw); \
    } \
  while (0)
#define USOCKET_CLR_SELECTABLE(sock, rw) \
  do \
    { \
      (sock)->select_condition &= ~(rw); \
    } \
  while (0)

#define IS_SUPPORTED_INET_DOMAIN(d) (((d) == AF_INET) || ((d) == AF_INET6))
#define IS_SUSPENDABLE_SOCK_TYPE(t) (((t) == SOCK_STREAM) \
                                  || ((t) == SOCK_DGRAM))

#define IS_STATE_SELECTABLE(s) (((s)->ctx.state != SOCKET_STATE_CLOSED)  \
                             && ((s)->ctx.state != SOCKET_STATE_PREALLOC) \
                             && ((s)->ctx.state != SOCKET_STATE_ABORTED) \
                             && ((s)->ctx.state != SOCKET_STATE_CLOSING) \
                             && ((s)->ctx.state != SOCKET_STATE_SUSPEND) \
                             && ((s)->ctx.state != SOCKET_STATE_OPEN))

#define IS_STATE_SUSPENDABLE(s) (((s)->ctx.state == SOCKET_STATE_PREALLOC)  \
                              || ((s)->ctx.state == SOCKET_STATE_OPENED) \
                              || ((s)->ctx.state == SOCKET_STATE_CONNECTED) \
                              || ((s)->ctx.state == SOCKET_STATE_LISTENED) \
                              || ((s)->ctx.state == SOCKET_STATE_SUSPEND))

#define IS_STATE_RESUMABLE(s) (((s)->ctx.state == SOCKET_STATE_PREALLOC) \
                            || ((s)->ctx.state == SOCKET_STATE_SUSPEND))

#define IS_STATE_READABLE(s) ((s)->select_condition & SELECT_READABLE)
#define IS_STATE_WRITABLE(s) ((s)->select_condition & SELECT_WRITABLE)

#define IS_SMS_SOCKET(s) ((s)->type == SOCK_SMS)

#define IS_VALID_ALTSOCKID(id) ((id) >= 0 && (id) < ALTCOM_NSOCKET)

#define _OUTPUT_ARG_MAX 7
#define _OPTVAL_LEN_MAX 16

#define usocket_smssock_writeready(d, s) \
  (usockif_sendtxready((d)->usockfd, USOCKET_USOCKID(s)))

/****************************************************************************
 * Public Data Type
 ****************************************************************************/

struct alt1250_s;

enum socket_state_e
{
  SOCKET_STATE_CLOSED = 0,
  SOCKET_STATE_PREALLOC,
  SOCKET_STATE_OPEN,
  SOCKET_STATE_OPENED,
  SOCKET_STATE_CONNECTING,
  SOCKET_STATE_WAITCONN,
  SOCKET_STATE_CONNECTED,
  SOCKET_STATE_LISTENED,
  SOCKET_STATE_SUSPEND,
  SOCKET_STATE_ABORTED,
  SOCKET_STATE_CLOSING
};

struct usock_addr_s
{
  uint32_t addrlen;
  struct sockaddr_storage addr;
};

struct usock_opt_s
{
  uint32_t optlen;
  uint8_t value[_OPTVAL_LEN_MAX];
};

struct usock_retention_ctx_s
{
  enum socket_state_e state;
  int altsockid;
};

struct usock_s
{
  struct usrsock_request_common_s request;
  int16_t domain;
  int16_t type;
  int16_t protocol;

  struct usock_retention_ctx_s ctx;
  enum socket_state_e old_state;
  int select_condition;
  int usockid;

  struct sms_refids_s refids;

  union sock_request_param_u
    {
      struct
        {
          /* store the input arguments of connect(), sendto(),
           * recvfrom(), bind(), accept(), getsockname()
           */

          struct usock_addr_s addr;

          /* store the input arguments of sendto(), recvfrom() */

          uint16_t buflen;
          int32_t flags;
        } addrbuflen;

      /* store the input arguments of listen() */

      uint16_t backlog;

      /* store the input arguments of setsockopt(), getsockopt() */

      struct
        {
          int16_t level;
          int16_t option;
          uint16_t optlen;
          uint8_t value[_OPTVAL_LEN_MAX];
        } opt;
    } sock_req;

  struct sock_reply_param_s
    {
      FAR void *outparams[_OUTPUT_ARG_MAX];

      int rep_result;
      int rep_errcode;

      union sock_reply_opt_u
        {
          /* store the output arguments of recvfrom(),
           * accept() ,getsockname()
           */

          struct usock_addr_s addr;

          /* store the input arguments of setsockopt(), getsockopt() */

          struct usock_opt_s opt;
        } sock_reply_opt;
    } sock_reply;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

FAR struct usock_s *usocket_search(FAR struct alt1250_s *dev, int usockid);
FAR struct usock_s *usocket_alloc(FAR struct alt1250_s *dev, int16_t type);
void usocket_free(FAR struct usock_s *sock);
void usocket_freeall(FAR struct alt1250_s *dev);
void usocket_commitstate(FAR struct alt1250_s *dev);
int usocket_smssock_num(FAR struct alt1250_s *dev);
void usocket_smssock_readready(FAR struct alt1250_s *dev);
void usocket_smssock_abort(FAR struct alt1250_s *dev);
#ifdef CONFIG_LTE_ALT1250_ENABLE_HIBERNATION_MODE
int usocket_active_sockets(FAR struct alt1250_s *dev);
struct usock_s *usocket_search_altsockid(FAR struct alt1250_s *dev,
                                         int altsockid,
                                         struct usock_s *self);
#endif

#endif  /* __APPS_LTE_ALT1250_ALT1250_SOCKET_H */
