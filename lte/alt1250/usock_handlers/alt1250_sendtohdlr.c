/****************************************************************************
 * apps/lte/alt1250/usock_handlers/alt1250_sendtohdlr.c
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
#include <nuttx/net/usrsock.h>

#include "alt1250_dbg.h"
#include "alt1250_container.h"
#include "alt1250_socket.h"
#include "alt1250_devif.h"
#include "alt1250_usockevent.h"
#include "alt1250_postproc.h"
#include "alt1250_usrsock_hdlr.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * name: send_sendto_command
 ****************************************************************************/

static int send_sendto_command(FAR struct alt1250_s *dev,
                               FAR struct alt_container_s *container,
                               FAR struct usock_s *usock,
                               int32_t flags, uint16_t addrlen,
                               uint16_t buflen,
                               FAR struct sockaddr_storage *addr,
                               FAR int32_t *usock_result)
{
  int idx = 0;
  FAR void *inparam[6];

  /* These members are referenced only when sending a command and
   * not when receiving a response, so local variables are used.
   */

  inparam[0] = &USOCKET_ALTSOCKID(usock);
  inparam[1] = &flags;
  inparam[2] = &addrlen;
  inparam[3] = &buflen;
  inparam[4] = addr;
  inparam[5] = dev->tx_buff;

  USOCKET_SET_RESPONSE(usock, idx++, USOCKET_REP_RESULT(usock));
  USOCKET_SET_RESPONSE(usock, idx++, USOCKET_REP_ERRCODE(usock));

  set_container_ids(container, USOCKET_USOCKID(usock), LTE_CMDID_SENDTO);
  set_container_argument(container, inparam, nitems(inparam));
  set_container_response(container, USOCKET_REP_RESPONSE(usock), idx);
  set_container_postproc(container, postproc_sockcommon, 0);

  return altdevice_send_command(dev, dev->altfd, container, usock_result);
}

/****************************************************************************
 * name: read_payload
 ****************************************************************************/

static int read_payload(FAR struct alt1250_s *dev,
                        FAR struct usock_s *usock,
                        FAR int32_t *usock_result)
{
  size_t sendlen;

  if (USOCKET_REQADDRLEN(usock) > 0)
    {
      *usock_result = usockif_readreqaddr(dev->usockfd,
                                          &USOCKET_REQADDR(usock),
                                          USOCKET_REQADDRLEN(usock));
      if (*usock_result < 0)
        {
          return REP_SEND_ACK;
        }
    }

  if (USOCKET_REQBUFLEN(usock) == 0)
    {
      *usock_result = 0;
      USOCKET_SET_SELECTABLE(usock, SELECT_WRITABLE);
      usocket_commitstate(dev);
      return REP_SEND_ACK;
    }

  sendlen = MIN(USOCKET_REQBUFLEN(usock), _TX_BUFF_SIZE);

  *usock_result = usockif_readreqsendbuf(dev->usockfd, dev->tx_buff,
                                         sendlen);
  if (*usock_result < 0)
    {
      return REP_SEND_ACK;
    }

  /* If the send size exceeds TX_BUFF_SIZE,
   * use seek to discard the exceeded buffer.
   */

  if (USOCKET_REQBUFLEN(usock) > sendlen)
    {
      usockif_discard(dev->usockfd, USOCKET_REQBUFLEN(usock) - sendlen);
    }

  return REP_NO_ACK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * name: nextstep_sendto
 ****************************************************************************/

int nextstep_sendto(FAR struct alt1250_s *dev,
                    FAR struct alt_container_s *reply,
                    FAR struct usock_s *usock,
                    FAR int32_t *usock_result,
                    FAR uint32_t *usock_xid,
                    FAR struct usock_ackinfo_s *ackinfo,
                    unsigned long arg)
{
  dbg_alt1250("%s start\n", __func__);

  return send_sendto_command(dev, reply, usock, USOCKET_REQFLAGS(usock),
                             USOCKET_REQADDRLEN(usock),
                             USOCKET_REQBUFLEN(usock),
                             (USOCKET_REQADDRLEN(usock) > 0 ?
                             &USOCKET_REQADDR(usock) : NULL),
                             usock_result);
}

/****************************************************************************
 * name: usockreq_sendto
 ****************************************************************************/

int usockreq_sendto(FAR struct alt1250_s *dev,
                    FAR struct usrsock_request_buff_s *req,
                    FAR int32_t *usock_result,
                    FAR uint32_t *usock_xid,
                    FAR struct usock_ackinfo_s *ackinfo)
{
  FAR struct usrsock_request_sendto_s *request = &req->request.send_req;
  FAR struct usock_s *usock;
  FAR struct alt_container_s *container;
  int ret = REP_SEND_ACK_WOFREE;

  dbg_alt1250("%s start\n", __func__);

  *usock_result = OK;
  *usock_xid = request->head.xid;

  usock = usocket_search(dev, request->usockid);
  if (usock == NULL)
    {
      dbg_alt1250("Failed to get socket context: %u\n",
                     request->usockid);
      *usock_result = -EBADFD;
      return REP_SEND_ACK_WOFREE;
    }

  USOCKET_SET_REQUEST(usock, request->head.reqid, request->head.xid);
  USOCKET_SET_REQBUFLEN(usock, request->buflen);
  USOCKET_SET_REQADDRLEN(usock, request->addrlen);
  USOCKET_SET_REQFLAGS(usock, request->flags);

  if (IS_SMS_SOCKET(usock))
    {
      ret = alt1250_sms_send(dev, request, usock, usock_result);
    }
  else
    {
      if (USOCKET_STATE(usock) == SOCKET_STATE_PREALLOC)
        {
          container = container_alloc();
          if (container == NULL)
            {
              dbg_alt1250("no container\n");
              return REP_NO_CONTAINER;
            }

          ret = read_payload(dev, usock, usock_result);
          if (ret == REP_SEND_ACK)
            {
              container_free(container);
              return ret;
            }

          ret = open_altsocket(dev, container, usock, usock_result);
          if (IS_NEED_CONTAINER_FREE(ret))
            {
              container_free(container);
            }
        }
      else if (USOCKET_STATE(usock) == SOCKET_STATE_SUSPEND)
        {
          dbg_alt1250("This socket has suspended: %u\n",
                      request->usockid);
          *usock_result = -EOPNOTSUPP;
          return REP_SEND_ACK_WOFREE;
        }
      else
        {
          /* Check if this socket is connected. */

          if ((SOCK_STREAM == USOCKET_TYPE(usock)) &&
              (USOCKET_STATE(usock) != SOCKET_STATE_CONNECTED))
            {
              dbg_alt1250("Unexpected state: %d\n", USOCKET_STATE(usock));
              *usock_result = -ENOTCONN;
              return REP_SEND_ACK_WOFREE;
            }

          container = container_alloc();
          if (container == NULL)
            {
              dbg_alt1250("no container\n");
              return REP_NO_CONTAINER;
            }

          ret = read_payload(dev, usock, usock_result);
          if (ret == REP_SEND_ACK)
            {
              container_free(container);
              return ret;
            }

          ret = nextstep_sendto(dev, container, usock, usock_result,
                                usock_xid, ackinfo, 0);
          if (IS_NEED_CONTAINER_FREE(ret))
            {
              container_free(container);
            }
        }
    }

  return ret;
}
