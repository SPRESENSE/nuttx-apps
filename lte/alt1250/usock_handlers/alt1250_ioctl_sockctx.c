/****************************************************************************
 * apps/lte/alt1250/usock_handlers/alt1250_ioctl_sockctx.c
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

#ifdef CONFIG_LTE_ALT1250_ENABLE_HIBERNATION_MODE

#include <nuttx/net/usrsock.h>

#include "alt1250_dbg.h"
#include "alt1250_container.h"
#include "alt1250_socket.h"
#include "alt1250_devif.h"
#include "alt1250_usockevent.h"
#include "alt1250_postproc.h"
#include "alt1250_usrsock_hdlr.h"
#include "alt1250_netdev.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ALT1250_SOCK_CTX_SIZE (sizeof(struct socket_ctx_data_s) + \
                               sizeof(struct usock_retention_ctx_s))

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int16_t g_request_level = SOL_SOCKET;
static int16_t g_request_option = SO_TYPE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * name: set_retention_ctx
 ****************************************************************************/

static void set_retention_ctx(FAR struct usock_s *usock,
                              FAR struct socket_context_s *req)
{
  FAR struct usock_retention_ctx_s *rctx =
    (FAR struct usock_retention_ctx_s *)req->ctx->s_priv;

  dbg_alt1250("set context: state:%d\n", rctx->state);
  dbg_alt1250("set context: altsockid:%d\n", rctx->altsockid);

  memcpy(&usock->ctx, rctx, sizeof(usock->ctx));
}

/****************************************************************************
 * name: get_resume_state
 ****************************************************************************/

static enum socket_state_e get_resume_state(FAR struct socket_context_s *req)
{
  FAR struct usock_retention_ctx_s *rctx;
  rctx = (FAR struct usock_retention_ctx_s *)req->ctx->s_priv;

  return rctx->state;
}

/****************************************************************************
 * name: get_resume_altsockid
 ****************************************************************************/

static int get_resume_altsockid(FAR struct socket_context_s *req)
{
  FAR struct usock_retention_ctx_s *rctx;
  rctx = (FAR struct usock_retention_ctx_s *)req->ctx->s_priv;

  return rctx->altsockid;
}

/****************************************************************************
 * name: is_valid_setsockctx
 ****************************************************************************/

static bool is_valid_setsockctx(FAR struct alt1250_s *dev,
                                FAR struct socket_context_s *req,
                                FAR struct usock_s *usock)
{
  FAR struct usock_retention_ctx_s *rctx;
  struct usock_s tmpsock;

  if (req->ctx == NULL)
    {
      return false;
    }

  dbg_alt1250("set context domain:%d, type:%d, proto:%d\n",
              req->ctx->s_domain, req->ctx->s_type, req->ctx->s_proto);

  if (!IS_SUPPORTED_INET_DOMAIN(req->ctx->s_domain))
    {
      return false;
    }

  if (!IS_SUSPENDABLE_SOCK_TYPE(req->ctx->s_type))
    {
      return false;
    }

  dbg_alt1250("set context state:%d, altsockid:%d\n",
              get_resume_state(req), get_resume_altsockid(req));

  rctx = (FAR struct usock_retention_ctx_s *)req->ctx->s_priv;
  memcpy(&tmpsock.ctx, rctx, sizeof(tmpsock.ctx));
  if (!IS_STATE_SUSPENDABLE(&tmpsock))
    {
      return false;
    }

  if (usocket_search_altsockid(dev, get_resume_altsockid(req), usock) !=
      NULL)
    {
      dbg_alt1250("There is the same altsockid:%d.\n", rctx->altsockid);
      return false;
    }

  return true;
}

/****************************************************************************
 * name: postproc_getsockopt
 ****************************************************************************/

static int postproc_getsockopt(FAR struct alt1250_s *dev,
                               FAR struct alt_container_s *reply,
                               FAR struct usock_s *usock,
                               FAR int32_t *usock_result,
                               FAR uint32_t *usock_xid,
                               FAR struct usock_ackinfo_s *ackinfo,
                               unsigned long arg)
{
  int ret = REP_SEND_ACK;
  FAR void **resp = CONTAINER_RESPONSE(reply);
  FAR struct socket_context_s *req = (FAR struct socket_context_s *)arg;

  dbg_alt1250("%s start\n", __func__);

  /* resp[0]: ret code
   * resp[1]: error code
   * resp[2]: optlen
   * resp[3]: optval
   */

  *usock_xid = USOCKET_XID(usock);
  *usock_result = COMBINE_ERRCODE(*(FAR int *)resp[0], *(FAR int *)resp[1]);

  dbg_alt1250("getsockopt result = %d\n", *usock_result);

  if (*usock_result >= 0)
    {
      int32_t type = *(FAR int32_t *)(resp[3]);

      dbg_alt1250("socket type = %d\n", type);

      if (type == req->ctx->s_type)
        {
          set_retention_ctx(usock, req);
          *usock_result = OK;

          if (USOCKET_STATE(usock) == SOCKET_STATE_CONNECTED)
            {
              usockif_sendconnected(dev->usockfd, USOCKET_USOCKID(usock));
            }
          else if (USOCKET_STATE(usock) == SOCKET_STATE_LISTENED)
            {
              usockif_sendconnected(dev->usockfd, USOCKET_USOCKID(usock));
              usockif_sendlistening(dev->usockfd, USOCKET_USOCKID(usock));
            }

          usocket_commitstate(dev);
        }
      else
        {
          *usock_result = -EINVAL;
        }
    }
  else
    {
      *usock_result = -EBADFD;
    }

  if (*usock_result < 0)
    {
      USOCKET_SET_ALTSOCKID(usock, -1);
    }

  return ret;
}

/****************************************************************************
 * name: is_valid_altsock
 ****************************************************************************/

static int is_valid_altsock(FAR struct alt1250_s *dev,
                            FAR struct alt_container_s *container,
                            FAR struct socket_context_s *req,
                            FAR struct usock_s *usock,
                            FAR int32_t *usock_result,
                            FAR uint32_t *usock_xid,
                            FAR struct usock_ackinfo_s *ackinfo)
{
  return send_getsockopt_command(dev, container, usock,
                                 g_request_level, g_request_option,
                                 sizeof(int), &g_request_level,
                                 &g_request_option, postproc_getsockopt,
                                 (unsigned long)req, usock_result);
}

/****************************************************************************
 * name: handle_getcontext
 ****************************************************************************/

static int handle_getcontext(FAR struct alt1250_s *dev,
                             FAR struct socket_context_s *req,
                             FAR struct usock_s *usock,
                             FAR int32_t *usock_result,
                             FAR uint32_t *usock_xid,
                             FAR struct usock_ackinfo_s *ackinfo)
{
  int ret = REP_SEND_ACK_WOFREE;
  FAR struct usock_retention_ctx_s *rctx;

  dbg_alt1250("%s start\n", __func__);

  *usock_result = OK;

  if (req->ctx_size == 0)
    {
      req->ctx_size = ALT1250_SOCK_CTX_SIZE;
    }
  else if (req->ctx_size >= ALT1250_SOCK_CTX_SIZE)
    {
      if (req->ctx == NULL)
        {
          *usock_result = -EINVAL;
        }
      else if (IS_STATE_SUSPENDABLE(usock))
        {
          rctx = (FAR struct usock_retention_ctx_s *)req->ctx->s_priv;
          req->ctx->s_domain = USOCKET_DOMAIN(usock);
          req->ctx->s_type = USOCKET_TYPE(usock);
          req->ctx->s_proto = USOCKET_PROTOCOL(usock);
          memcpy(rctx, &usock->ctx, sizeof(usock->ctx));

          if (USOCKET_STATE(usock) == SOCKET_STATE_SUSPEND)
            {
              rctx->state = USOCKET_OLDSTATE(usock);
            }
          else
            {
              USOCKET_SET_OLDSTATE(usock, USOCKET_STATE(usock));
              USOCKET_SET_STATE(usock, SOCKET_STATE_SUSPEND);
              usocket_commitstate(dev);
              usockif_sendrxready(dev->usockfd, USOCKET_USOCKID(usock));
              usockif_sendtxready(dev->usockfd, USOCKET_USOCKID(usock));
            }
        }
      else
        {
          *usock_result = -EBUSY;
        }
    }
  else
    {
      *usock_result = -ENOSPC;
    }

  if (*usock_result == OK)
    {
      ret = REP_SEND_DACK;
      ackinfo->valuelen = sizeof(struct socket_context_s);
      ackinfo->valuelen_nontrunc = sizeof(struct socket_context_s);
      ackinfo->value_ptr = (FAR uint8_t *)req;
      ackinfo->buf_ptr = NULL;
    }

  return ret;
}

/****************************************************************************
 * name: handle_setcontext
 ****************************************************************************/

static int handle_setcontext(FAR struct alt1250_s *dev,
                             FAR struct socket_context_s *req,
                             FAR struct usock_s *usock,
                             FAR int32_t *usock_result,
                             FAR uint32_t *usock_xid,
                             FAR struct usock_ackinfo_s *ackinfo)
{
  int ret = REP_SEND_ACK_WOFREE;
  FAR struct alt_container_s *container;

  dbg_alt1250("%s start\n", __func__);
  dbg_alt1250("set context size: %d\n", req->ctx_size);

  if (req->ctx_size >= ALT1250_SOCK_CTX_SIZE)
    {
      if (!is_valid_setsockctx(dev, req, usock))
        {
          *usock_result = -EINVAL;
        }
      else if (IS_STATE_RESUMABLE(usock))
        {
          if (get_resume_state(req) == SOCKET_STATE_PREALLOC)
            {
              /* Skip validation of ALT1250 socket */

              dbg_alt1250("skip socket validation\n");

              set_retention_ctx(usock, req);
              *usock_result = OK;
            }
          else
            {
              container = container_alloc();
              if (container == NULL)
                {
                  dbg_alt1250("no container\n");
                  return REP_NO_CONTAINER;
                }

              USOCKET_SET_ALTSOCKID(usock, get_resume_altsockid(req));
              ret = is_valid_altsock(dev, container, req, usock,
                                     usock_result, usock_xid, ackinfo);
              if (IS_NEED_CONTAINER_FREE(ret))
                {
                  container_free(container);
                }

              if (*usock_result < 0)
                {
                  USOCKET_SET_ALTSOCKID(usock, -1);
                }
            }
        }
      else
        {
          *usock_result = -EOPNOTSUPP;
        }
    }
  else
    {
      *usock_result = -EINVAL;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * name: usockreq_ioctl_sockctx
 ****************************************************************************/

int usockreq_ioctl_sockctx(FAR struct alt1250_s *dev,
                           FAR struct usrsock_request_buff_s *req,
                           FAR int32_t *usock_result,
                           FAR uint32_t *usock_xid,
                           FAR struct usock_ackinfo_s *ackinfo)
{
  FAR struct usrsock_request_ioctl_s *request = &req->request.ioctl_req;
  FAR struct socket_context_s *sctxreq = &req->req_ioctl.sctxreq;

  FAR struct usock_s *usock;
  int ret = REP_SEND_ACK_WOFREE;

  dbg_alt1250("%s start\n", __func__);

  *usock_result = -EBADFD;
  *usock_xid = request->head.xid;

  usock = usocket_search(dev, request->usockid);
  if (usock)
    {
      if (request->cmd == SIOCGETCONTEXT)
        {
          ret = handle_getcontext(dev, sctxreq, usock, usock_result,
                                  usock_xid, ackinfo);
        }
      else if (request->cmd == SIOCSETCONTEXT)
        {
          ret = handle_setcontext(dev, sctxreq, usock, usock_result,
                                  usock_xid, ackinfo);
        }
    }

  return ret;
}

#endif
