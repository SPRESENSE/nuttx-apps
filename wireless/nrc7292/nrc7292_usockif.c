/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_usockif.c
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
#include <errno.h>
#include <nuttx/net/usrsock.h>

#include "nrc7292_daemon.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: send_usock_ack_common
 ****************************************************************************/

int send_usock_ack_common(int fd,
                          uint16_t events,
                          uint16_t flags,
                          uint32_t xid,
                          int16_t result)
{
  struct usrsock_message_req_ack_s resp;
  int ret;

  memset(&resp, 0, sizeof(resp));
  resp.result = result;
  resp.head.msgid = USRSOCK_MESSAGE_RESPONSE_ACK;
  resp.head.flags = flags;
  resp.head.events = events;
  resp.xid = xid;

  ret = write(fd, &resp, sizeof(resp));

  if (ret < 0)
    {
      return -errno;
    }
  else
    {
      return 0;
    }
}

/****************************************************************************
 * Name: send_usock_ack_nodata
 ****************************************************************************/

int send_usock_ack_nodata(int fd,
                          uint32_t xid,
                          int16_t ret)
{
  return send_usock_ack_common(fd, 0, 0, xid, ret);
}

/****************************************************************************
 * Name: send_usock_ack_in_progress
 ****************************************************************************/

int send_usock_ack_in_progress(int fd,
                               uint32_t xid,
                               int16_t ret)
{
  return send_usock_ack_common(fd, 0, USRSOCK_MESSAGE_FLAG_REQ_IN_PROGRESS,
                               xid, ret);
}

/****************************************************************************
 * Name: send_usock_ack_with_data
 ****************************************************************************/

int send_usock_ack_with_data(int fd,
                             uint32_t xid,
                             int result,
                             FAR uint8_t *data,
                             size_t data_len)
{
  struct usrsock_message_datareq_ack_s resp;
  int ret;

  memset(&resp, 0, sizeof(resp));
  resp.reqack.result = result;
  resp.reqack.head.msgid = USRSOCK_MESSAGE_RESPONSE_DATA_ACK;
  resp.reqack.head.flags = 0;
  resp.reqack.head.events = 0;
  resp.reqack.xid = xid;
  resp.valuelen_nontrunc = data_len;
  resp.valuelen = data_len;

  ret = write(fd, &resp, sizeof(resp));

  if (ret < 0)
    {
      goto error;
    }

  if (0 < data_len)
    {
      ret = write(fd, data, data_len);
    }

error:
  if (ret < 0)
    {
      return -errno;
    }
  else
    {
      return 0;
    }
}

/****************************************************************************
 * Name: send_usock_ack
 ****************************************************************************/

int send_usock_ack(FAR struct nrc7292_s *priv, int ret)
{
  if (ret == 0 && 0 < priv->atcmd_ctx.resp_len)
    {
      return send_usock_ack_with_data(priv->atcmd_ctx.fd,
                                      priv->atcmd_ctx.xid,
                                      ret,
                                      (FAR uint8_t *) &priv->atcmd_ctx.resp,
                                      priv->atcmd_ctx.resp_len);
    }
  else
    {
      return send_usock_ack_nodata(priv->atcmd_ctx.fd,
                                   priv->atcmd_ctx.xid,
                                   ret);
    }
}

/****************************************************************************
 * Name: send_usock_ack_fin
 ****************************************************************************/

int send_usock_ack_fin(FAR struct nrc7292_s *priv, int ret)
{
  send_usock_ack(priv, ret);
  return ATCMD_FIN;
}

/****************************************************************************
 * Name: send_usock_ack_with_packet
 ****************************************************************************/

int send_usock_ack_with_packet(int fd,
                               uint32_t xid,
                               int ret,
                               FAR uint8_t *addr,
                               size_t addr_len,
                               FAR uint8_t *data,
                               size_t data_len)
{
  struct usrsock_message_datareq_ack_s resp;

  memset(&resp, 0, sizeof(resp));
  resp.reqack.result = ret;
  resp.reqack.head.msgid = USRSOCK_MESSAGE_RESPONSE_DATA_ACK;
  resp.reqack.head.flags = 0;
  resp.reqack.head.events = 0;
  resp.reqack.xid = xid;
  resp.valuelen_nontrunc = 0;
  resp.valuelen = 0;

  if (addr != NULL)
    {
      resp.valuelen_nontrunc = addr_len;
      resp.valuelen = addr_len;
    }

  /* send header */

  ret = write(fd, &resp, sizeof(resp));

  if (ret < 0)
    {
      goto done;
    }

  /* send addr */

  if (addr != NULL)
    {
      ret = write(fd, addr, addr_len);
      if (ret < 0)
        {
          goto done;
        }
    }

  /* send data */

  if (0 < data_len)
    {
      ret = write(fd, data, data_len);
    }

done:
  if (ret < 0)
    {
      return -errno;
    }

  return 0;
}

/****************************************************************************
 * Name: send_usock_data_ack_with_addr
 ****************************************************************************/

int send_usock_data_ack_with_addr(int fd,
                                  uint32_t xid,
                                  int ret,
                                  FAR uint8_t *addr,
                                  size_t addr_len,
                                  FAR uint8_t *data,
                                  size_t data_len)
{
  struct usrsock_message_datareq_ack_s resp;

  memset(&resp, 0, sizeof(resp));
  resp.reqack.result = ret;
  resp.reqack.head.msgid = USRSOCK_MESSAGE_RESPONSE_DATA_ACK;
  resp.reqack.head.flags = 0;
  resp.reqack.head.events = 0;
  resp.reqack.xid = xid;
  resp.valuelen_nontrunc = 0;
  resp.valuelen = 0;

  if (addr != NULL)
    {
      resp.valuelen_nontrunc = addr_len;
      resp.valuelen = addr_len;
    }

  /* send header */

  ret = write(fd, &resp, sizeof(resp));

  if (ret < 0)
    {
      goto done;
    }

  /* send addr */

  if (addr != NULL)
    {
      ret = write(fd, addr, addr_len);

      if (ret < 0)
        {
          goto done;
        }
    }

  /* send data */

  if (0 < data_len)
    {
      ret = write(fd, data, data_len);
    }

done:
  if (ret < 0)
    {
      return -errno;
    }

  return 0;
}

/****************************************************************************
 * Name: send_usock_socket_events
 ****************************************************************************/

int send_usock_socket_events(int fd,
                             int events,
                             int usockid)
{
  FAR struct usrsock_message_socket_event_s event;
  int ret;

  memset(&event, 0, sizeof(event));
  event.head.flags = USRSOCK_MESSAGE_FLAG_EVENT;
  event.head.msgid = USRSOCK_MESSAGE_SOCKET_EVENT;
  event.usockid = usockid;
  event.head.events = events;

  ret = write(fd, &event, sizeof(event));

  if (ret < 0)
    {
      return -errno;
    }

  return 0;
}

