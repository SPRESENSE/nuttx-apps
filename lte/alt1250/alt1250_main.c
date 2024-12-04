/****************************************************************************
 * apps/lte/alt1250/alt1250_main.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <assert.h>
#include <sys/poll.h>

#include <nuttx/net/dns.h>

#include "alt1250_dbg.h"
#include "alt1250_daemon.h"
#include "alt1250_devif.h"
#include "alt1250_devevent.h"
#include "alt1250_usockif.h"
#include "alt1250_usockevent.h"
#include "alt1250_container.h"
#include "alt1250_select.h"
#include "alt1250_usrsock_hdlr.h"
#include "alt1250_evt.h"
#include "alt1250_netdev.h"
#include "alt1250_sms.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SYNC_CMD_PREFIX "-s"

#define ALTFDNO (0)
#define USOCKFDNO (1)

#define SET_POLLIN(fds, fid) \
  do \
    { \
      (fds).fd = (fid);  \
      (fds).events = POLLIN; \
    } \
  while (0)

#define IS_POLLIN(fds) ((fds).revents & POLLIN)

#define ALT1250_CTX_SIZE (offsetof(struct alt1250_s, checksum) + \
                          sizeof(dev->checksum))

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct alt1250_s *g_daemon = NULL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: notify_to_lapi_caller
 ****************************************************************************/

static void notify_to_lapi_caller(sem_t *syncsem)
{
  /* has -s (sync) option? */

  if (syncsem)
    {
      /* Notify release to lapi waiting for synchronization */

      sem_post(syncsem);
    }
}

/****************************************************************************
 * Name: initialize_daemon
 ****************************************************************************/

static int initialize_daemon(FAR struct alt1250_s *dev)
{
  int ret;

  /* Initialize sub-system */

  /* Event call back task must be started
   * before any device files are opened
   */

  ret = alt1250_evttask_start();
  ASSERT(ret > 0);

  dev->altfd = init_alt1250_device();
  ASSERT(dev->altfd >= 0);

  dev->usockfd = init_usock_device();
  ASSERT(dev->usockfd >= 0);

  ret = altdevice_seteventbuff(dev->altfd, init_event_buffer());
  ASSERT(ret >= 0);

  init_containers();
  init_selectcontainer(dev);
  alt1250_sms_initcontainer(dev);
  alt1250_netdev_register(dev);

  return OK;
}

/****************************************************************************
 * Name: finalize_daemon
 ****************************************************************************/

static void finalize_daemon(FAR struct alt1250_s *dev)
{
  alt1250_netdev_unregister(dev);
  alt1250_evtdatadestroy();
  finalize_alt1250_device(dev->altfd);
  finalize_usock_device(dev->usockfd);
  alt1250_evttask_stop(dev);
}

/****************************************************************************
 * Name: alt1250_loop
 ****************************************************************************/

static int alt1250_loop(FAR struct alt1250_s *dev)
{
  int ret;
  int pw_stat;
  struct pollfd fds[2];
  nfds_t nfds;
  bool is_running = true;

  initialize_daemon(dev);

  /* Get modem power status. If modem is turned on, it means resuming from
   * hibernation mode.
   */

  pw_stat = altdevice_powercontrol(dev->altfd, LTE_CMDID_GET_POWER_STAT);

  dbg_alt1250("Modem power status = %d\n", pw_stat);

  if (pw_stat)
    {
      dev->is_resuming = true;
    }
  else
    {
      dev->is_resuming = false;
    }

  notify_to_lapi_caller(dev->syncsem);

  /* Main loop of this daemon */

  while (is_running)
    {
      memset(fds, 0, sizeof(fds));

      SET_POLLIN(fds[ALTFDNO], dev->altfd);
      nfds = 1;

      /* if (!dev->is_usockrcvd && !dev->recvfrom_processing) */

      if (ACCEPT_USOCK_REQUEST(dev))
        {
          SET_POLLIN(fds[USOCKFDNO], dev->usockfd);
          nfds++;
        }

      ret = poll(fds, nfds, -1);
      ASSERT(ret > 0);
      ret = OK;

      if (IS_POLLIN(fds[ALTFDNO]))
        {
          ret = perform_alt1250events(dev);
        }

      dbg_alt1250("ret: %u, recvfrom_processing: %d,"
                  " IS_POLLIN: %d, is_usockrcvd: %d\n",
                  ret, dev->recvfrom_processing,
                  IS_POLLIN(fds[USOCKFDNO]), dev->is_usockrcvd);

      if ((ret != REP_MODEM_RESET) && (!dev->recvfrom_processing)
          && (IS_POLLIN(fds[USOCKFDNO]) || dev->is_usockrcvd))
        {
          switch (perform_usockrequest(dev))
            {
              case REP_SEND_TERM:
                is_running = false;
                break;

              case REP_NO_CONTAINER:

                /* Do nothing because request could
                 * not send to modem driver because of
                 * no more container. To wait for empty container.
                 */

                break;

              default:
                dev->is_usockrcvd = false;
                break;
            }
        }
    }

  finalize_daemon(dev);

  return OK;
}

#ifdef CONFIG_LTE_ALT1250_ENABLE_HIBERNATION_MODE
/****************************************************************************
 * Name: calc_checksum
 ****************************************************************************/

static uint16_t calc_checksum(FAR uint8_t *ptr, uint16_t len)
{
  uint32_t ret = 0x00;
  uint32_t calctmp = 0x00;
  uint16_t i;

  /* Data accumulating */

  for (i = 0; i < len; i++)
    {
      calctmp += ptr[i];
    }

  ret = ~((calctmp & 0xffff) + (calctmp >> 16));

  return (uint16_t)ret;
}

/****************************************************************************
 * Name: apply_context
 ****************************************************************************/

static int apply_context(FAR struct alt1250_s *dev,
                         FAR uint8_t *ctx, int len)
{
  int remlen = ALT1250_CTX_SIZE - dev->stored_ctx_size;

  if (remlen < len)
    {
      return ERROR;
    }

  memcpy((FAR uint8_t *)dev + dev->stored_ctx_size, ctx, len);
  dev->stored_ctx_size += len;
  remlen -= len;

  return remlen;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int ret;
  FAR char *endptr;
  FAR sem_t *syncsem = NULL;

  if (argc > 1)
    {
      /* The format is "-sXXXXXXXX".
       * XXXXXXXXX indicates the pointer address to the semaphore
       * that will be posted at the timing when the daemon opens the
       * usersock device.
       */

      if (!(strncmp(argv[1], SYNC_CMD_PREFIX, strlen(SYNC_CMD_PREFIX))))
        {
          syncsem = (FAR sem_t *)strtol(&argv[1][strlen(SYNC_CMD_PREFIX)],
            &endptr, 16);
          if (!syncsem || endptr == &argv[1][strlen(SYNC_CMD_PREFIX)] ||
            *endptr != '\0')
            {
              return -EINVAL;
            }
        }
    }

  if (g_daemon)
    {
      fprintf(stderr, "%s is already running! \n", argv[0]);
      notify_to_lapi_caller(syncsem);
      return -1;
    }

  g_daemon = calloc(sizeof(struct alt1250_s), 1);
  ASSERT(g_daemon);

  g_daemon->syncsem = syncsem;
  g_daemon->evtq = (mqd_t)-1;
  g_daemon->sid = -1;
  g_daemon->is_usockrcvd = false;
  g_daemon->usock_enable = TRUE;
  g_daemon->is_support_lwm2m = false;
  g_daemon->lwm2m_apply_xid = -1;
  g_daemon->api_enable = true;
  g_daemon->context_cb = NULL;
  MODEM_STATE_POFF(g_daemon);

  reset_fwupdate_info(g_daemon);

  ret = alt1250_loop(g_daemon);
  free(g_daemon);
  g_daemon = NULL;

  /* Notify lapi that Daemon has finished */

  notify_to_lapi_caller(syncsem);

  return ret;
}

#ifdef CONFIG_LTE_ALT1250_ENABLE_HIBERNATION_MODE
int alt1250_set_api_enable(FAR struct alt1250_s *dev, bool enable)
{
  if (!dev)
    {
      return ERROR;
    }

  dev->api_enable = enable;

  return OK;
}

int alt1250_is_api_in_progress(FAR struct alt1250_s *dev)
{
  if (!dev)
    {
      return ERROR;
    }

  return dev->is_usockrcvd ? 1 : 0;
}

int alt1250_set_context_save_cb(FAR struct alt1250_s *dev,
                                context_save_cb_t context_cb)
{
  if (!dev)
    {
      return ERROR;
    }

  dev->context_cb = context_cb;

  return OK;
}

int alt1250_collect_daemon_context(FAR struct alt1250_s *dev)
{
  if (!dev)
    {
      return ERROR;
    }

  if (!dev->context_cb)
    {
      return ERROR;
    }

  /* Copy connection information */

  dev->d_flags = dev->net_dev.d_flags;

#ifdef CONFIG_NET_IPv4
  memcpy(&dev->d_ipaddr, &dev->net_dev.d_ipaddr, sizeof(in_addr_t));
  memcpy(&dev->d_draddr, &dev->net_dev.d_draddr, sizeof(in_addr_t));
  memcpy(&dev->d_netmask, &dev->net_dev.d_netmask, sizeof(in_addr_t));
#endif
#ifdef CONFIG_NET_IPv6
  memcpy(&dev->d_ipv6addr, &dev->net_dev.d_ipv6addr, sizeof(net_ipv6addr_t));
  memcpy(&dev->d_ipv6draddr,
         &dev->net_dev.d_ipv6draddr,
         sizeof(net_ipv6addr_t));
  memcpy(&dev->d_ipv6netmask,
         &dev->net_dev.d_ipv6netmask,
         sizeof(net_ipv6addr_t));
#endif

  /* Save checksum without checksum for validation */

  dev->checksum = calc_checksum((FAR uint8_t *)dev,
                                offsetof(struct alt1250_s, checksum));

  /* Call user application callback */

  dev->context_cb((FAR uint8_t *)dev,
                  offsetof(struct alt1250_s, checksum) +
                  sizeof(dev->checksum));

  return OK;
}

int alt1250_apply_daemon_context(FAR struct alt1250_s *dev,
                                 FAR uint8_t *ctx, int len)
{
  uint16_t checksum;
#if defined(CONFIG_NETDB_DNSCLIENT)
  uint8_t ndnsaddrs = ALTCOM_DNS_SERVERS;
  int i;
#endif
  int remlen;

  if (!dev)
    {
      return ERROR;
    }

  remlen = apply_context(dev, ctx, len);
  if (remlen != 0)
    {
      return remlen;
    }

  /* Calc checksum without checksum parameter */

  checksum = calc_checksum((FAR uint8_t *)dev,
                           offsetof(struct alt1250_s, checksum));
  if (dev->checksum != checksum)
    {
      dbg_alt1250("Saved context is invalid.\n");
      return ERROR;
    }

  /* Set APN related pointers */

  dev->apn.apn = dev->apn_name;
  dev->apn.user_name = dev->user_name;
  dev->apn.password = dev->pass;

  /* Copy connection information */

  dev->net_dev.d_flags = dev->d_flags;

#ifdef CONFIG_NET_IPv4
  memcpy(&dev->net_dev.d_ipaddr, &dev->d_ipaddr, sizeof(in_addr_t));
  memcpy(&dev->net_dev.d_draddr, &dev->d_draddr, sizeof(in_addr_t));
  memcpy(&dev->net_dev.d_netmask, &dev->d_netmask, sizeof(in_addr_t));
#endif
#ifdef CONFIG_NET_IPv6
  memcpy(&dev->net_dev.d_ipv6addr, &dev->d_ipv6addr, sizeof(net_ipv6addr_t));
  memcpy(&dev->net_dev.d_ipv6draddr,
         &dev->d_ipv6draddr,
         sizeof(net_ipv6addr_t));
  memcpy(&dev->net_dev.d_ipv6netmask,
         &dev->d_ipv6netmask,
         sizeof(net_ipv6addr_t));
#endif

#if defined(CONFIG_NETDB_DNSCLIENT)
  for (i = 0; (i < ndnsaddrs) && (i < CONFIG_NETDB_DNSSERVER_NAMESERVERS);
      i++)
    {
      FAR const struct sockaddr *dnsaddr =
        (FAR const struct sockaddr *)&dev->dnsaddrs[i];
      socklen_t addrlen;

      if (dnsaddr->sa_family == AF_INET || dnsaddr->sa_family == AF_INET6)
        {
          addrlen = (dnsaddr->sa_family == AF_INET) ?
            sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
          dns_add_nameserver(dnsaddr, addrlen);
        }
    }

  dns_set_queryfamily(dev->dnsqfamily);
#endif
  return OK;
}

void alt1250_save_dnsaddr(FAR struct sockaddr_storage *dnsaddr, int index)
{
  memcpy(&g_daemon->dnsaddrs[index], dnsaddr,
         sizeof(struct sockaddr_storage));
}

void alt1250_save_dnsqfamily(int dnsqfamily)
{
  g_daemon->dnsqfamily = dnsqfamily;
}
#endif
