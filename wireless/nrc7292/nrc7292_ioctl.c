/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_ioctl.c
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
#include <math.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include <nuttx/net/usrsock.h>
#include <nuttx/wireless/wireless.h>
#include <nuttx/wireless/nrc7292.h>
#include <nuttx/net/nrc7292.h>

#include "nrc7292_daemon.h"
#include "nrc7292_ioctl.h"
#include "nrc7292_usockif.h"
#include "nrc7292_atcmd.h"
#include "nrc7292_state.h"
#include "nrc7292_regdom.h"
#include "nrc7292_event.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NRC7292_IW_EVENT_SIZE(field) \
  (offsetof(struct iw_event, u) + sizeof(((FAR union iwreq_data *)0)->field))

/****************************************************************************
 * Private Data Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int connect_ap(FAR struct nrc7292_s *priv, FAR const char *ifname);
static int disconnect_ap(FAR struct nrc7292_s *priv, FAR const char *ifname);
static int start_ap(FAR struct nrc7292_s *priv, FAR const char *ifname);
static int stop_ap(FAR struct nrc7292_s *priv, FAR const char *ifname);
static int start_relay(FAR struct nrc7292_s *priv, FAR const char *ifname);
static int stop_relay(FAR struct nrc7292_s *priv, FAR const char *ifname);
static int procresp_dhcp(FAR struct nrc7292_s *priv,
                         FAR struct atcmd_resp_tokens_s *resp);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: freq2double
 ****************************************************************************/

static inline double freq2double(FAR const struct iw_freq *freq)
{
  return ((double) freq->m) * pow(10, freq->e);
}

/****************************************************************************
 * Name: double2freq
 ****************************************************************************/

static inline void double2freq(double freq, FAR struct iw_freq *iwf)
{
  iwf->e = 0;

  while (freq < 1e6)
    {
      freq *= 10;
      iwf->e--;
    }

  while (freq >= 10e6)
    {
      freq /= 10;
      iwf->e++;
    }

  iwf->m = (int) freq;
}

/****************************************************************************
 * Name: copy_arg2resp
 ****************************************************************************/

static inline void copy_arg2resp(FAR struct nrc7292_s *priv, FAR void *arg,
                                 size_t size)
{
  memcpy(&priv->atcmd_ctx.resp, arg, size);
  priv->atcmd_ctx.resp_len = size;
}

/****************************************************************************
 * Name: term_daemon
 ****************************************************************************/

static void term_daemon(FAR struct nrc7292_s *priv)
{
  struct wapi_event_cb_args_s arg;

  memset(&arg, 0, sizeof(arg));
  arg.event = WAPI_EVENT_TERMINATED;
  notify_event_to_apps(priv, &arg);

  priv->to_terminate = true;
}

/****************************************************************************
 * Name: procresp_getipaddr
 ****************************************************************************/

static int procresp_getipaddr(FAR struct nrc7292_s *priv,
                              FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct ifreq *ifr = &priv->atcmd_ctx.resp.ifr;
  FAR struct sockaddr_in *ifaddr = (FAR struct sockaddr_in *) &ifr->ifr_addr;

  if (0 < resp->count)
    {
      ifaddr->sin_addr.s_addr = inet_addr(resp->tokens[0]);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: notify_ipv4addr_updated_event
 ****************************************************************************/

static void notify_ipv4addr_updated_event(FAR struct nrc7292_s *priv,
                                          FAR struct in_addr *ipaddr,
                                          FAR struct in_addr *netmask)
{
  struct wapi_event_cb_args_s arg;
  uint16_t prefixlen = 0;
  uint32_t mask;

  memset(&arg, 0, sizeof(arg));
  arg.event = WAPI_EVENT_IPADDR_UPDATED;
  arg.u.ipaddr.u.ipv4 = *ipaddr;

  mask = ntohl(netmask->s_addr);

  while (mask)
    {
      prefixlen += (mask & 1);
      mask >>= 1;
    }

  arg.u.ipaddr.prefixlen = prefixlen;

  notify_event_to_apps(priv, &arg);
}

/****************************************************************************
 * Name: procret_do_ifconfig
 ****************************************************************************/

static int procret_do_ifconfig(FAR struct nrc7292_s *priv, int ret)
{
  if (ret == 0)
    {
      set_ipinfo_to_driver(priv, true);
    }

  send_usock_ack(priv, ret);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: do_ifconfig
 ****************************************************************************/

static int do_ifconfig(FAR struct nrc7292_s *priv, FAR char *ifname)
{
  char ipaddr[INET_ADDRSTRLEN];
  char netmask[INET_ADDRSTRLEN];
  char draddr[INET_ADDRSTRLEN];
  int ret = OK;

  if (strncmp(ifname, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  inet_ntoa_r(priv->ifconfig.ipaddr, ipaddr, sizeof(ipaddr));
  inet_ntoa_r(priv->ifconfig.netmask, netmask, sizeof(netmask));
  inet_ntoa_r(priv->ifconfig.draddr, draddr, sizeof(draddr));

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   NULL, NULL,
                   procret_do_ifconfig,
                   "AT+WIPADDR=\"%s\",\"%s\",\"%s\"\r\n",
                   ipaddr, netmask, draddr);
error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: do_dhcp
 ****************************************************************************/

static int do_dhcp(FAR struct nrc7292_s *priv, FAR char *ifname)
{
  int ret = OK;

  if (strncmp(ifname, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "WDHCP", procresp_dhcp,
                   send_usock_ack_fin,
                   "AT+WDHCP\r\n");

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: proc_setipaddr
 ****************************************************************************/

static int proc_setipaddr(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct ifreq *ifr;
  FAR struct sockaddr_in *sin;

  ifr = (FAR struct ifreq *) arg;
  sin = (FAR struct sockaddr_in *) &ifr->ifr_addr;
  priv->ifconfig.ipaddr = sin->sin_addr;

  return do_ifconfig(priv, ifr->ifr_name);
}

/****************************************************************************
 * Name: proc_getipaddr
 ****************************************************************************/

static int proc_getipaddr(FAR struct nrc7292_s *priv, FAR void *arg)
{
  int ret;

  copy_arg2resp(priv, arg, sizeof(struct ifreq));

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                    "WIPADDR", procresp_getipaddr,
                    send_usock_ack_fin,
                    "AT+WIPADDR?\r\n");
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: procresp_getnetmask
 ****************************************************************************/

static int procresp_getnetmask(FAR struct nrc7292_s *priv,
                               FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct ifreq *ifr = &priv->atcmd_ctx.resp.ifr;
  FAR struct sockaddr_in *ifaddr
    = (FAR struct sockaddr_in *) &ifr->ifr_netmask;

  if (1 < resp->count)
    {
      ifaddr->sin_addr.s_addr = inet_addr(resp->tokens[1]);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: proc_setnetmask
 ****************************************************************************/

static int proc_setnetmask(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct ifreq *ifr = (FAR struct ifreq *) arg;
  FAR struct sockaddr_in *sin = (FAR struct sockaddr_in *) &ifr->ifr_addr;

  priv->ifconfig.netmask = sin->sin_addr;

  return do_ifconfig(priv, ifr->ifr_name);
}

/****************************************************************************
 * Name: proc_getnetmask
 ****************************************************************************/

static int proc_getnetmask(FAR struct nrc7292_s *priv, FAR void *arg)
{
  int ret;
  FAR struct ifreq *ifr = (FAR struct ifreq *) arg;

  if (strncmp(ifr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  copy_arg2resp(priv, arg, sizeof(struct ifreq));

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                    "WIPADDR", procresp_getnetmask,
                    send_usock_ack_fin,
                    "AT+WIPADDR?\r\n");

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: procresp_gethwaddr
 ****************************************************************************/

static int procresp_gethwaddr(FAR struct nrc7292_s *priv,
                              FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct ifreq *ifr = &priv->atcmd_ctx.resp.ifr;
  FAR char *mac;

  mac = ifr->ifr_hwaddr.sa_data;
  sscanf(resp->tokens[0], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
         &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

  ioctl(priv->drv_fd, SIOCSIFHWADDR, ifr);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: proc_gethwaddr
 ****************************************************************************/

static int proc_gethwaddr(FAR struct nrc7292_s *priv, FAR void *arg)
{
  int ret;
  FAR struct ifreq *ifr = (FAR struct ifreq *) arg;

  if (strncmp(ifr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  copy_arg2resp(priv, arg, sizeof(struct ifreq));

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                    "WMACADDR", procresp_gethwaddr,
                    send_usock_ack_fin,
                    "AT+WMACADDR?\r\n");

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: proc_getifflags
 ****************************************************************************/

static int proc_getifflags(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct ifreq *ifr = &priv->atcmd_ctx.resp.ifr;

  copy_arg2resp(priv, arg, sizeof(struct ifreq));

  ioctl(priv->drv_fd, SIOCGIFFLAGS, ifr);

  return send_usock_ack(priv, OK);
}

/****************************************************************************
 * Name: proc_setifflags
 ****************************************************************************/

static int proc_setifflags(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct ifreq *ifr = (FAR struct ifreq *) arg;
  int ret = OK;

  if (strncmp(ifr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  switch (priv->wconfig[STA].conf.sta_mode)
    {
    case WAPI_MODE_MANAGED:
      if (IFF_IS_UP(ifr->ifr_flags))
        {
          connect_ap(priv, ifr->ifr_name);
        }
      else
        {
          disconnect_ap(priv, ifr->ifr_name);
        }
      break;

    case WAPI_MODE_MASTER:
      if (IFF_IS_UP(ifr->ifr_flags))
        {
          start_ap(priv, ifr->ifr_name);
        }
      else
        {
          stop_ap(priv, ifr->ifr_name);
        }
      break;

    case WAPI_MODE_RELAY:
      if (IFF_IS_UP(ifr->ifr_flags))
        {
          start_relay(priv, ifr->ifr_name);
        }
      else
        {
          stop_relay(priv, ifr->ifr_name);
        }
      break;

    default:
      ret = -EINVAL;
      break;
    }

error:
  if (ret < 0)
    {
      send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: procresp_getgateway
 ****************************************************************************/

static int procresp_getgateway(FAR struct nrc7292_s *priv,
                               FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct ifreq *ifr = &priv->atcmd_ctx.resp.ifr;
  FAR struct sockaddr_in *ifaddr
    = (FAR struct sockaddr_in *) &ifr->ifr_dstaddr;

  if (2 < resp->count)
    {
      ifaddr->sin_addr.s_addr = inet_addr(resp->tokens[2]);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: proc_setgateway
 ****************************************************************************/

static int proc_setgateway(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct ifreq *ifr = (FAR struct ifreq *) arg;
  FAR struct sockaddr_in *sin = (FAR struct sockaddr_in *) &ifr->ifr_addr;

  priv->ifconfig.draddr = sin->sin_addr;

  return do_ifconfig(priv, ifr->ifr_name);
}

/****************************************************************************
 * Name: proc_getgateway
 ****************************************************************************/

static int proc_getgateway(FAR struct nrc7292_s *priv, FAR void *arg)
{
  int ret;
  FAR struct ifreq *ifr;

  ifr = (FAR struct ifreq *) arg;

  if (strncmp(ifr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  copy_arg2resp(priv, arg, sizeof(struct ifreq));

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "WIPADDR", procresp_getgateway,
                   send_usock_ack_fin,
                   "AT+WIPADDR?\r\n");

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: procresp_getsensitivity
 ****************************************************************************/

static int procresp_getsensitivity(FAR struct nrc7292_s *priv,
                                   FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;
  int rssi;

  rssi = strtol(resp->tokens[0], NULL, 10);
  iwr->u.sens.value = -rssi;

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: proc_getsensitivity
 ****************************************************************************/

static int proc_getsensitivity(FAR struct nrc7292_s *priv,
                                     FAR void *arg)
{
  int ret;
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  copy_arg2resp(priv, arg, sizeof(struct iwreq));

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "WRXSIG", procresp_getsensitivity,
                   send_usock_ack_fin,
                   "AT+WRXSIG?\r\n");

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: proc_setiwap
 ****************************************************************************/

static int proc_setiwap(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;

  memcpy((FAR void *) &priv->wconfig[STA].conf.bssid[0],
         iwr->u.ap_addr.sa_data, 6);

  return send_usock_ack(priv, OK);
}

/****************************************************************************
 * Name: proc_getiwap
 ****************************************************************************/

static int proc_getiwap(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;

  copy_arg2resp(priv, arg, sizeof(struct iwreq));
  memcpy(iwr->u.ap_addr.sa_data, priv->wconfig[STA].conf.bssid, 6);

  return send_usock_ack(priv, OK);
}

/****************************************************************************
 * Name: procresp_getcountry
 ****************************************************************************/

static int procresp_getcountry(FAR struct nrc7292_s *priv,
                               FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;
  FAR char *country;

  country = (FAR char *) iwr->u.data.pointer;
  strncpy(priv->country, resp->tokens[0], 2);
  strncpy(country, resp->tokens[0], 2);
  priv->country[2] = country[2] = '\0';

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: proc_setcountry
 ****************************************************************************/

static int proc_setcountry(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  char country[3];
  int ret;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  strncpy(country, iwr->u.data.pointer, 2);
  country[2] = '\0';

  if (!is_valid_country(country, &priv->board_info))
    {
      ret = -EINVAL;
      goto error;
    }

  strncpy(priv->country, country, 2);
  priv->country[2] = '\0';

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "WCOUNTRY", NULL,
                   send_usock_ack_fin,
                   "AT+WCOUNTRY=\"%s\"\r\n", priv->country);

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: proc_getcountry
 ****************************************************************************/

static int proc_getcountry(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  int ret;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  copy_arg2resp(priv, arg, sizeof(struct iwreq));

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "WCOUNTRY", procresp_getcountry,
                   send_usock_ack_fin,
                   "AT+WCOUNTRY?\r\n");

error:
  if (ret < 0)
    {
      send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: proc_getiwmode
 ****************************************************************************/

static int proc_getiwmode(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;

  copy_arg2resp(priv, arg, sizeof(struct iwreq));
  iwr->u.mode = priv->wconfig[STA].conf.sta_mode;

  return send_usock_ack(priv, OK);
}

/****************************************************************************
 * Name: proc_setiwmode
 ****************************************************************************/

static int proc_setiwmode(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  struct ifreq ifr;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) == 0)
    {
      priv->wconfig[STA].conf.sta_mode = iwr->u.mode;
      strcpy(ifr.ifr_name, RELAY_IFNAME);

      if (iwr->u.mode == IW_MODE_RELAY)
        {
          ioctl(priv->drv_fd, SIOCSIFADDIFACE, &ifr);
        }
      else
        {
          ioctl(priv->drv_fd, SIOCSIFDELIFACE, &ifr);
        }
    }

  return send_usock_ack(priv, OK);
}

/****************************************************************************
 * Name: proc_getiwfreq
 ****************************************************************************/

static int proc_getiwfreq(FAR struct nrc7292_s *priv, void *arg)
{
  struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;

  copy_arg2resp(priv, arg, sizeof(struct iwreq));

  switch (priv->wconfig[STA].conf.flag)
    {
    case WAPI_FREQ_AUTO:
      iwr->u.freq.flags = IW_FREQ_AUTO;
      break;

    case WAPI_FREQ_FIXED:
      iwr->u.freq.flags = IW_FREQ_FIXED;
      break;
    }

  double2freq(priv->wconfig[STA].conf.freq, &(iwr->u.freq));

  return send_usock_ack(priv, OK);
}

/****************************************************************************
 * Name: proc_setiwfreq
 ****************************************************************************/

static int proc_setiwfreq(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr;
  double freq;
  int ret = OK;

  iwr = (FAR struct iwreq *) arg;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  freq = freq2double(&(iwr->u.freq));

  if (!is_valid_freq(priv->country,
                     (uint32_t) freq, priv->wconfig[STA].ext.bandwidth))
    {
      ret = -ECHRNG;
      goto error;
    }

  priv->wconfig[STA].conf.freq = freq;

  if (IW_FREQ_AUTO == iwr->u.freq.flags)
    {
      priv->wconfig[STA].conf.flag = WAPI_FREQ_AUTO;
    }
  else if (IW_FREQ_FIXED == iwr->u.freq.flags)
    {
      priv->wconfig[STA].conf.flag = WAPI_FREQ_FIXED;
    }
  else
    {
      ret = -EINVAL;
    }

error:
  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: get_bandwidth
 ****************************************************************************/

static FAR const char *
freq2bwstr(FAR struct nrc7292_s *priv, uint16_t bw)
{
  FAR char *bandwidth = "";

  if (bw == 2000)
    {
      bandwidth = "@2";
    }
  else if (bw == 4000)
    {
      bandwidth = "@4";
    }
  else
    {
      bandwidth = "@1";
    }

  return bandwidth;
}

/****************************************************************************
 * Name: procret_start_ap
 ****************************************************************************/

static int procret_start_ap(FAR struct nrc7292_s *priv, int ret)
{
  int atcmd_ret;

  if (ret == 0)
    {
#ifdef CONFIG_WIRELESS_NRC7292_HALOW_AP_DHCPSERVER

      exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                 "WDHCPS", procresp_dhcp,
                 send_usock_ack_fin,
                 "AT+WDHCPS\r\n");
      atcmd_ret = ATCMD_CONTINUE;

#else

      send_usock_ack(priv, ret);
      atcmd_ret = ATCMD_FIN;

#endif
      update_wireless_state(priv, WIRELESS_AP_ENABLED);
    }
  else
    {
      send_usock_ack(priv, -EPERM);
      atcmd_ret = ATCMD_FIN;
    }

  return atcmd_ret;
}

/****************************************************************************
 * Name: start_ap
 ****************************************************************************/

static int start_ap(FAR struct nrc7292_s *priv, FAR const char *ifname)
{
  FAR char *auth = NULL;
  FAR const char *bandwidth = NULL;
  int freq_khz;
  int freq_mhz;
  int freq_mhz_frac;
  int ret;

  if (strncmp(ifname, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  if (priv->wconfig[STA].conf.sta_mode != WAPI_MODE_MASTER)
    {
      ret = -EINVAL;
      goto error;
    }

  if ((priv->wconfig[STA].conf.auth_wpa == 0) ||
      ((priv->wconfig[STA].conf.auth_wpa
        & IW_AUTH_WPA_VERSION_DISABLED) != 0))
    {
      auth = "open";
    }
  else if ((priv->wconfig[STA].conf.auth_wpa
            & IW_AUTH_WPA_VERSION_WPA2) != 0)
    {
      auth = "wpa2-psk";
    }
  else
    {
      ret = -EINVAL;
      goto error;
    }

  bandwidth = freq2bwstr(priv, priv->wconfig[STA].ext.bandwidth);
  freq_khz = (int) priv->wconfig[STA].conf.freq;
  freq_mhz = freq_khz / 1000;
  freq_mhz_frac = (0 < (freq_khz % 1000)) ? 5 : 0;

  if (0 < priv->wconfig[STA].conf.phraselen)
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       NULL, NULL,
                       procret_start_ap,
                       "AT+WSOFTAP=%d.%d%s,\"%s\",\"%s\",\"%s\"\r\n",
                       freq_mhz, freq_mhz_frac, bandwidth,
                       priv->wconfig[STA].conf.ssid,
                       auth, priv->wconfig[STA].conf.passphrase);
    }
  else
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       NULL, NULL,
                       procret_start_ap,
                       "AT+WSOFTAP=%d.%d%s,\"%s\",\"%s\"\r\n",
                       freq_mhz, freq_mhz_frac, bandwidth,
                       priv->wconfig[STA].conf.ssid,
                       auth);
    }

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: procresp_stop_wireless
 ****************************************************************************/

static int procresp_stop_wireless(FAR struct nrc7292_s *priv,
                                  FAR struct atcmd_resp_tokens_s *resp)
{
  update_wireless_state(priv, WIRELESS_DISCONNECTED);
  send_usock_ack(priv, OK);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: stop_ap
 ****************************************************************************/

static int stop_ap(FAR struct nrc7292_s *priv, FAR const char *ifname)
{
  int ret = OK;

  if (strncmp(ifname, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "BOOT", procresp_stop_wireless,
                   NULL,
                   "ATZ\r\n");

error:
  if (ret < 0)
    {
      send_usock_ack(priv, ret);
    }
  else
    {
      priv->suppress_crushed_event = true;
    }

  return OK;
}

/****************************************************************************
 * Name: procret_start_relay
 ****************************************************************************/

static int procret_start_relay(FAR struct nrc7292_s *priv, int ret)
{
  int retval = OK;

  if (ret == OK)
    {
      update_wireless_state(priv, WIRELESS_RELAY_ENABLED);
    }
  else
    {
      retval = -ENOTCONN;
    }

  send_usock_ack(priv, retval);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: start_relay
 ****************************************************************************/

static int start_relay(FAR struct nrc7292_s *priv, FAR const char *ifname)
{
  int ret;
  int i;

  if (strncmp(ifname, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  if (priv->wconfig[STA].conf.sta_mode != WAPI_MODE_RELAY)
    {
      ret = -EINVAL;
      goto error;
    }

  /* Only open mode is available on NRC7292. */

  for (i = 0; i < 2; i++)
    {
      if ((priv->wconfig[i].conf.auth_wpa != 0)
          && (priv->wconfig[i].conf.auth_wpa != IW_AUTH_WPA_VERSION_DISABLED))
        {
          ret = -EINVAL;
          goto error;
        }
    }

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   NULL, NULL,
                   procret_start_relay,
                   "AT+WRELAY=\"%s\",\"%s\"\r\n",
                   priv->wconfig[AP].conf.ssid,
                   priv->wconfig[STA].conf.ssid);

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: stop_relay
 ****************************************************************************/

static int stop_relay(FAR struct nrc7292_s *priv, FAR const char *ifname)
{
  int ret = OK;

  if (strncmp(ifname, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "BOOT", procresp_stop_wireless,
                   NULL,
                   "ATZ\r\n");

error:
  if (ret < 0)
    {
      send_usock_ack(priv, ret);
    }
  else
    {
      priv->suppress_crushed_event = true;
    }

  return OK;
}

/****************************************************************************
 * Name: procret_disconnect_ap
 ****************************************************************************/

static int procret_disconnect_ap(FAR struct nrc7292_s *priv, int ret)
{
  int retval;

  if (ret == OK)
    {
      update_wireless_state(priv, WIRELESS_DISCONNECTED);
      retval = OK;
    }
  else
    {
      retval = -EPERM;
    }

  send_usock_ack(priv, retval);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: disconnect_ap
 ****************************************************************************/

static int disconnect_ap(FAR struct nrc7292_s *priv, FAR const char *ifname)
{
  int ret;

  if (strncmp(ifname, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "WDISCONN", NULL,
                   procret_disconnect_ap,
                   "AT+WDISCONN\r\n");

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: proc_connect
 ****************************************************************************/

static int proc_connect(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr;
  int ret;

  iwr = (FAR struct iwreq *) arg;

  if (iwr->u.param.value == TRUE)
    {
      ret = connect_ap(priv, iwr->ifr_name);
    }
  else
    {
      ret = disconnect_ap(priv, iwr->ifr_name);
    }

  return ret;
}

/****************************************************************************
 * Name: procret_scan_ack
 ****************************************************************************/

static int procret_scan_ack(FAR struct nrc7292_s *priv, int ret)
{
  if (ret == 0)
    {
      update_scan_state(priv, SCAN_DONE);

      ignore_atcmd_response(&priv->atcmd_ctx);
    }
  else
    {
      update_scan_state(priv, SCAN_STOPPED);
    }

  send_usock_ack(priv, ret);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: get_encode_from_scan_result
 ****************************************************************************/

static int get_encode_from_scan_result(FAR char *enc_mode)
{
  int encode = 0;

  if ((strstr(enc_mode, "CCMP") != NULL)
      || (strstr(enc_mode, "TKIP") != NULL))
    {
      encode = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
    }
  else
    {
      encode = IW_ENCODE_DISABLED;
    }

  return encode;
}

/****************************************************************************
 * Name: parse_scan_result
 ****************************************************************************/

static void parse_scan_result(FAR struct atcmd_resp_tokens_s *resp,
                              FAR uint8_t *bssid, FAR double *freq,
                              FAR int *rssi, FAR enum wapi_mode_e *mode,
                              FAR int *encode, FAR char *essid)
{
  if (resp->count < 5)
    {
      return;
    }

  sscanf(resp->tokens[0], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
         &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
  *freq = strtod(resp->tokens[1], NULL);
  sscanf(resp->tokens[2], "%d", rssi);
  *mode = WAPI_MODE_MASTER;
  *encode = get_encode_from_scan_result(resp->tokens[3]);
  strncpy(essid, resp->tokens[4], 32);
}

/****************************************************************************
 * Name: procresp_scan_result
 ****************************************************************************/

static int procresp_scan_result(FAR struct nrc7292_s *priv,
                                FAR struct atcmd_resp_tokens_s *resp)
{
  enum wapi_mode_e mode = WAPI_MODE_MANAGED;
  FAR FAR struct iw_event *iwe;
  int encode = 0;
  uint8_t bssid[6];
  double freq = 0.0;
  int rssi = 0;
  char essid[32 + 1];
  ssize_t essid_len;
  uint16_t len;

  parse_scan_result(resp, bssid, &freq, &rssi, &mode, &encode, essid);

  /* BSSID */

  len = NRC7292_IW_EVENT_SIZE(ap_addr);

  if (SCAN_RES_MAX_SIZE < priv->scan_results_size + len)
    {
      goto error;
    }

  iwe = (FAR struct iw_event *) &priv->scan_results[priv->scan_results_size];
  iwe->len = len;
  iwe->cmd = SIOCGIWAP;
  memcpy(iwe->u.ap_addr.sa_data, bssid, 6);
  priv->scan_results_size += len;

  /* ESSID */

  len = NRC7292_IW_EVENT_SIZE(essid);
  essid_len = strnlen(essid, 32);
  len += (essid_len + 3) & -4;

  if (SCAN_RES_MAX_SIZE < priv->scan_results_size + len)
    {
      goto error;
    }

  iwe = (FAR struct iw_event *) &priv->scan_results[priv->scan_results_size];
  iwe->len = len;
  iwe->cmd = SIOCGIWESSID;
  iwe->u.essid.flags = 0;
  iwe->u.essid.length = essid_len;
  iwe->u.essid.pointer = (FAR void *) sizeof(iwe->u.essid);
  memcpy(&iwe->u.essid + 1, essid, essid_len);
  priv->scan_results_size += len;

  /* EVQUAL */

  len = NRC7292_IW_EVENT_SIZE(qual);

  if (SCAN_RES_MAX_SIZE < priv->scan_results_size + len)
    {
      goto error;
    }

  iwe = (FAR struct iw_event *) &priv->scan_results[priv->scan_results_size];
  iwe->len = len;
  iwe->cmd = IWEVQUAL;
  iwe->u.qual.level = rssi;
  iwe->u.qual.noise = 0x00;
  iwe->u.qual.updated = IW_QUAL_DBM | IW_QUAL_ALL_UPDATED;
  priv->scan_results_size += len;

  /* mode */

  len = NRC7292_IW_EVENT_SIZE(mode);

  if (SCAN_RES_MAX_SIZE < priv->scan_results_size + len)
    {
      goto error;
    }

  iwe = (FAR struct iw_event *) &priv->scan_results[priv->scan_results_size];
  iwe->len = len;
  iwe->cmd = SIOCGIWMODE;
  iwe->u.mode = mode;
  priv->scan_results_size += len;

  /* encode */

  len = NRC7292_IW_EVENT_SIZE(data);

  if (SCAN_RES_MAX_SIZE < priv->scan_results_size + len)
    {
      goto error;
    }

  iwe = (FAR struct iw_event *) &priv->scan_results[priv->scan_results_size];
  iwe->len = len;
  iwe->cmd = SIOCGIWENCODE;
  iwe->u.data.flags = encode;
  iwe->u.data.length = 0;
  iwe->u.essid.pointer = NULL;
  priv->scan_results_size += len;

  /* freq */

  len = NRC7292_IW_EVENT_SIZE(freq);

  if (SCAN_RES_MAX_SIZE < priv->scan_results_size + len)
    {
      goto error;
    }

  iwe = (FAR struct iw_event *) &priv->scan_results[priv->scan_results_size];
  iwe->len = len;
  iwe->cmd = SIOCGIWFREQ;
  double2freq(freq, &iwe->u.freq);
  priv->scan_results_size += len;

error:
  return ATCMD_FIN;
}

/****************************************************************************
 * Name: proc_setiwscan
 ****************************************************************************/

static int proc_setiwscan(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  FAR struct iw_scan_req *req
    = (FAR struct iw_scan_req *) iwr->u.data.pointer;
  int ret;

  if (0 < req->essid_len && (iwr->u.data.flags & IW_SCAN_THIS_ESSID))
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                        "WSCANSSID", procresp_scan_result,
                        procret_scan_ack,
                       "AT+WSCANSSID=\"%s\"\r\n",
                       req->essid);
    }
  else
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       "WSCAN", procresp_scan_result,
                       procret_scan_ack,
                       "AT+WSCAN\r\n");
    }

  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }
  else
    {
      update_scan_state(priv, SCAN_RUNNING);
      return OK;
    }
}

/****************************************************************************
 * Name: proc_getiwscan
 ****************************************************************************/

static int proc_getiwscan(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;
  int ret;

  copy_arg2resp(priv, arg, sizeof(struct iwreq));

  if (priv->scan_state == SCAN_DONE)
    {
      if (iwr->u.data.length < priv->scan_results_size)
        {
          ret = -E2BIG;
        }
      else
        {
          memcpy(iwr->u.data.pointer, priv->scan_results,
                 priv->scan_results_size);
          iwr->u.data.length = priv->scan_results_size;
          priv->atcmd_ctx.resp_len = sizeof(*iwr);
          ret = OK;
        }
    }
  else if (priv->scan_state == SCAN_RUNNING)
    {
      ret = -EAGAIN;
    }
  else
    {
      ret = -EINVAL;
    }

  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: proc_getiwessid
 ****************************************************************************/

static int proc_getiwessid(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;

  copy_arg2resp(priv, arg, sizeof(struct iwreq));
  strncpy(iwr->u.essid.pointer, priv->wconfig[STA].conf.ssid,
          priv->wconfig[STA].conf.ssidlen);
  iwr->u.essid.length = priv->wconfig[STA].conf.ssidlen;
  iwr->u.essid.flags = WAPI_ESSID_ON;

  return send_usock_ack(priv, OK);
}

/****************************************************************************
 * Name: get_error_code
 ****************************************************************************/

static int get_error_code(FAR struct nrc7292_s *priv,
                          FAR struct atcmd_resp_tokens_s *resp)
{
  priv->atcmd_ctx.return_error_code = strtol(resp->tokens[0], NULL, 10);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procret_reconnect_ap
 ****************************************************************************/

static int procret_reconnect_ap(FAR struct nrc7292_s *priv, int ret)
{
  return connect_ap(priv, WLAN_IFNAME);
}

/****************************************************************************
 * Name: notify_dhcp_timeout_event
 ****************************************************************************/

void notify_dhcp_timeout_event(FAR struct nrc7292_s *priv)
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
 * Name: procret_get_connect_info
 ****************************************************************************/

static int procresp_get_connect_info(FAR struct nrc7292_s *priv,
                         FAR struct atcmd_resp_tokens_s *resp)
{
  if (1 < resp->count)
    {
      sscanf(resp->tokens[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &priv->bssid[0], &priv->bssid[1], &priv->bssid[2],
             &priv->bssid[3], &priv->bssid[4], &priv->bssid[5]);
    }

  update_wireless_state(priv, WIRELESS_CONNECTED);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procret_connect_ap
 ****************************************************************************/

static int procret_connect_ap(FAR struct nrc7292_s *priv, int ret)
{
  int atcmd_ret;

  if (ret == 0)
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       "WCONN", procresp_get_connect_info,
                       send_usock_ack_fin,
                       "AT+WCONN?\r\n");
      if (ret < 0)
        {
          send_usock_ack(priv, OK);
          atcmd_ret = ATCMD_FIN;
        }
      else
        {
          atcmd_ret = ATCMD_CONTINUE;
        }
    }
  else
    {
      /* The case that a host has already connected to AP */

      if (priv->atcmd_ctx.return_error_code == 2)
        {
          priv->atcmd_ctx.return_error_code = 0;
          ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                            NULL, NULL,
                            procret_reconnect_ap,
                            "AT+WDISCONN\r\n");
          if (ret < 0)
            {
              send_usock_ack(priv, OK);
              atcmd_ret = ATCMD_FIN;
            }
          else
            {
              atcmd_ret = ATCMD_CONTINUE;
            }
        }
      else
        {
          update_wireless_state(priv, WIRELESS_DISCONNECTED);
          send_usock_ack(priv, -EIO);
          atcmd_ret = ATCMD_FIN;
        }
    }

  return atcmd_ret;
}

/****************************************************************************
 * Name: is_all_zero
 ****************************************************************************/

static bool is_all_zero(FAR const char *buf, size_t len)
{
  int i;

  for (i = 0; i < len; i++)
    {
      if (buf[i] != 0x00)
        {
          return false;
        }
    }

  return true;
}

/****************************************************************************
 * Name: connect_ap
 ****************************************************************************/

static int connect_ap(FAR struct nrc7292_s *priv, FAR const char *ifname)
{
  FAR char *auth = NULL;
  int ret;
  int atcmd_ret = ATCMD_FIN;
  char ssid[WAPI_ESSID_MAX_SIZE + 1];

  if (strncmp(ifname, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  if ((priv->wconfig[STA].conf.auth_wpa == 0) ||
      ((priv->wconfig[STA].conf.auth_wpa
        & IW_AUTH_WPA_VERSION_DISABLED) != 0))
    {
      auth = "open";
    }
  else if ((priv->wconfig[STA].conf.auth_wpa
            & IW_AUTH_WPA_VERSION_WPA2) != 0)
    {
      auth = "wpa2-psk";
    }
  else if ((priv->wconfig[STA].conf.auth_wpa
            & IW_AUTH_WPA_VERSION_WPA3) != 0)
    {
      if (priv->wconfig[STA].conf.phraselen == 0)
        {
          auth = "wpa3-owe";
        }
      else
        {
          auth = "wpa3-sae";
        }
    }
  else
    {
      ret = -EINVAL;
      goto error;
    }

  memset(ssid, 0, sizeof(ssid));

  if (is_all_zero(priv->wconfig[STA].conf.bssid, 6))
    {
      strncpy(ssid, priv->wconfig[STA].conf.ssid, WAPI_ESSID_MAX_SIZE);
    }
  else
    {
      snprintf(ssid, WAPI_ESSID_MAX_SIZE, "%02x:%02x:%02x:%02x:%02x:%02x",
               priv->wconfig[STA].conf.bssid[0],
               priv->wconfig[STA].conf.bssid[1],
               priv->wconfig[STA].conf.bssid[2],
               priv->wconfig[STA].conf.bssid[3],
               priv->wconfig[STA].conf.bssid[4],
               priv->wconfig[STA].conf.bssid[5]);
    }

  if (0 < priv->wconfig[STA].conf.phraselen)
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       "WCONN", get_error_code,
                       procret_connect_ap,
                       "AT+WCONN=\"%s\",\"%s\",\"%s\"\r\n",
                       ssid, auth,
                       priv->wconfig[STA].conf.passphrase);
    }
  else
    {
      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       "WCONN", get_error_code,
                       procret_connect_ap,
                       "AT+WCONN=\"%s\",\"%s\"\r\n",
                       ssid, auth);
    }

  atcmd_ret = (ret < 0) ? ATCMD_FIN: ATCMD_CONTINUE;

error:
  if (ret < 0)
    {
      send_usock_ack(priv, ret);
    }

  return atcmd_ret;
}

/****************************************************************************
 * Name: proc_setiwessid
 ****************************************************************************/

static int proc_setiwessid(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  int conf_id;
  int ret = OK;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) == 0)
    {
      conf_id = STA;
    }
  else if (strncmp(iwr->ifr_name, RELAY_IFNAME, RELAY_IFNAME_LEN) == 0)
    {
      conf_id = AP;
    }
  else
    {
      ret = -EINVAL;
      goto error;
    }

  strncpy((FAR char *) priv->wconfig[conf_id].conf.ssid,
          iwr->u.essid.pointer, WAPI_ESSID_MAX_SIZE);
  priv->wconfig[conf_id].conf.ssidlen
    = strnlen(priv->wconfig[conf_id].conf.ssid, WAPI_ESSID_MAX_SIZE);

error:
  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: procresp_gettxpower
 ****************************************************************************/

static int procresp_gettxpower(FAR struct nrc7292_s *priv,
                               FAR struct atcmd_resp_tokens_s *resp)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;
  int32_t power;

  power = strtol(resp->tokens[0], NULL, 10);
  iwr->u.txpower.flags = IW_TXPOW_DBM;
  iwr->u.txpower.value = power;

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: proc_gettxpower
 ****************************************************************************/

static int proc_gettxpower(FAR struct nrc7292_s *priv, FAR void *arg)
{
  int ret;

  copy_arg2resp(priv, arg, sizeof(struct iwreq));

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   "WTXPOWER", procresp_gettxpower,
                   send_usock_ack_fin,
                   "AT+WTXPOWER?\r\n");
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: proc_settxpower
 ****************************************************************************/

static int proc_settxpower(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  int ret;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) != 0)
    {
      ret = -EINVAL;
      goto error;
    }

  if (iwr->u.txpower.flags != WAPI_TXPOWER_DBM)
    {
      ret = -ENOTSUP;
      goto error;
    }

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   NULL, NULL,
                   send_usock_ack_fin,
                   "AT+WTXPOWER=%ld\r\n", iwr->u.txpower.value);

error:
  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: proc_getiwauth
 ****************************************************************************/

static int proc_getiwauth(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;
  int conf_id = STA;
  int ret = OK;

  copy_arg2resp(priv, arg, sizeof(struct iwreq));

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) == 0)
    {
      conf_id = STA;
    }
  else if (strncmp(iwr->ifr_name, RELAY_IFNAME, RELAY_IFNAME_LEN) == 0)
    {
      conf_id = AP;
    }
  else
    {
      ret = -EINVAL;
      goto error;
    }

  switch (iwr->u.param.flags)
    {
    case IW_AUTH_WPA_VERSION:
      iwr->u.param.value = priv->wconfig[conf_id].conf.auth_wpa;
      break;

    case IW_AUTH_CIPHER_PAIRWISE:
      iwr->u.param.value = priv->wconfig[conf_id].conf.cipher_mode;
      break;

    default:
      ret = -EINVAL;
      break;
    }

error:
  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: proc_setiwauth
 ****************************************************************************/

static int proc_setiwauth(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  int conf_id = STA;
  int ret = OK;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) == 0)
    {
      conf_id = STA;
    }
  else if (strcmp(iwr->ifr_name, RELAY_IFNAME) == 0)
    {
      conf_id = AP;
    }
  else
    {
      ret = -EINVAL;
      goto error;
    }

  switch (iwr->u.param.flags)
    {
    case IW_AUTH_WPA_VERSION:
      priv->wconfig[conf_id].conf.auth_wpa = iwr->u.param.value;
      break;

    case IW_AUTH_CIPHER_PAIRWISE:
      priv->wconfig[conf_id].conf.cipher_mode = iwr->u.param.value;
      break;

    default:
      ret = -EINVAL;
      break;
    }

error:
  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: proc_getiwencodeext
 ****************************************************************************/

static int proc_getiwencodeext(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;
  FAR struct iw_encode_ext *ext;
  int conf_id = STA;
  int ret = OK;

  copy_arg2resp(priv, arg, sizeof(struct iwreq));

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) == 0)
    {
      conf_id = STA;
    }
  else if (strncmp(iwr->ifr_name, RELAY_IFNAME, RELAY_IFNAME_LEN) == 0)
    {
      conf_id = AP;
    }
  else
    {
      ret = -EINVAL;
      goto error;
    }

  ext = iwr->u.encoding.pointer;

  if (0 < priv->wconfig[conf_id].conf.phraselen)
    {
      memcpy(ext->key, priv->wconfig[conf_id].conf.passphrase,
             priv->wconfig[conf_id].conf.phraselen);
      ext->key_len = priv->wconfig[conf_id].conf.phraselen;
    }
  else
    {
      ext->key_len = 0;
    }

  switch (priv->wconfig[conf_id].conf.alg)
    {
      case WPA_ALG_NONE:
        ext->alg = IW_ENCODE_ALG_NONE;
        break;

      case WPA_ALG_WEP:
        ext->alg = IW_ENCODE_ALG_WEP;
        break;

      case WPA_ALG_TKIP:
        ext->alg = IW_ENCODE_ALG_TKIP;
        break;

      case WPA_ALG_CCMP:
        ext->alg = IW_ENCODE_ALG_CCMP;
        break;

      default:
        ret = -EINVAL;
        break;
    }

error:
  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: proc_setiwencodeext
 ****************************************************************************/

static int proc_setiwencodeext(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  FAR struct iw_encode_ext *ext
    = (FAR struct iw_encode_ext *) iwr->u.encoding.pointer;
  int conf_id = STA;
  int ret = OK;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) == 0)
    {
      conf_id = STA;
    }
  else if (strncmp(iwr->ifr_name, RELAY_IFNAME, RELAY_IFNAME_LEN) == 0)
    {
      conf_id = AP;
    }
  else
    {
      ret = -EINVAL;
      goto error;
    }

  switch (ext->alg)
    {
    case IW_ENCODE_ALG_NONE:
      priv->wconfig[conf_id].conf.alg = WPA_ALG_NONE;
      break;

    case IW_ENCODE_ALG_WEP:
      priv->wconfig[conf_id].conf.alg = WPA_ALG_WEP;
      break;

    case IW_ENCODE_ALG_TKIP:
      priv->wconfig[conf_id].conf.alg = WPA_ALG_TKIP;
      break;

    case IW_ENCODE_ALG_CCMP:
      priv->wconfig[conf_id].conf.alg = WPA_ALG_CCMP;
      break;

    default:
      ret = -EINVAL;
      goto error;
    }

  if (0 < ext->key_len)
    {
      memcpy((void *) priv->wconfig[conf_id].conf.passphrase, ext->key,
             ext->key_len);
      priv->wconfig[conf_id].conf.phraselen = ext->key_len;
    }

error:
  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: proc_setiwbandwidth
 ****************************************************************************/

static int proc_setiwbandwidth(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  int ret = OK;

  if (strcmp(iwr->ifr_name, WLAN_IFNAME) == 0)
    {
      priv->wconfig[STA].ext.bandwidth = iwr->u.param.value;
    }
  else
    {
      ret = -EINVAL;
    }

  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: proc_getiwbandwidth
 ****************************************************************************/

static int proc_getiwbandwidth(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = &priv->atcmd_ctx.resp.iwr;
  int ret = OK;

  if (strncmp(iwr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) == 0)
    {
      copy_arg2resp(priv, arg, sizeof(struct iwreq));
      iwr->u.param.value = priv->wconfig[STA].ext.bandwidth;
    }
  else
    {
      ret = -EINVAL;
    }

  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: proc_setfwupdate_init
 ****************************************************************************/

static int proc_setfwupdate_init(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  int ret;

  priv->fwupdate_ctx.total_len = (size_t) iwr->u.param.value;
  priv->fwupdate_ctx.fw_len = 0;
  priv->fwupdate_ctx.written_len = 0;
  priv->fwupdate_ctx.set_crc = false;

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   NULL, NULL,
                   send_usock_ack_fin,
                   "AT+FWUPDATE=0\r\n");

  if (ret < 0)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: procresp_fwinject
 ****************************************************************************/

static int procresp_fwinject(FAR struct nrc7292_s *priv,
                             FAR struct atcmd_resp_tokens_s *resp)
{
  uint32_t written_segment_len;
  int ret = OK;

  if (resp->count < 2)
    {
      ret = -EINVAL;
      goto error;
    }

  if (strncmp(resp->tokens[0], "FWBINDL_IDLE", 12) == 0)
    {
      if (4 <= resp->count)
        {
          nrc7292_printf("retry_count: %s\n", resp->tokens[3]);
        }
      else
        {
          ret = -EINVAL;
        }
    }
  else if (strncmp(resp->tokens[0], "FWBINDL_DROP", 12) == 0)
    {
      ret = OK;
    }
  else if (strncmp(resp->tokens[0], "FWBINDL_DONE", 12) == 0)
    {
      if (2 <= resp->count)
        {
          written_segment_len = strtoul(resp->tokens[1], NULL, 10);
          priv->fwupdate_ctx.written_len += (size_t) written_segment_len;
          ret = written_segment_len;
        }
      else
        {
          ret = -EINVAL;
        }
    }
  else
    {
      ret = -EINVAL;
    }

error:
  send_usock_ack(priv, ret);
  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procret_fwinject
 ****************************************************************************/

static int procret_fwinject(FAR struct nrc7292_s *priv, int ret)
{
  if (ret == 0)
    {
      send_atcmd_rawdata(priv->drv_fd,
                         priv->fwupdate_ctx.segment,
                         priv->fwupdate_ctx.segment_len);
    }
  else
    {
      send_usock_ack(priv, ret);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procret_fwinject
 ****************************************************************************/

static int procret_initfwupdate(FAR struct nrc7292_s *priv, int ret)
{
  if (ret == 0)
    {
      send_usock_ack(priv, priv->fwupdate_ctx.segment_len);
    }
  else
    {
      send_usock_ack(priv, -EINVAL);
    }

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: init_fwupdate
 ****************************************************************************/

static int init_fwupdate(FAR struct nrc7292_s *priv,
                         FAR uint8_t *buf, size_t len)
{
  static const size_t CRC32_STR_LEN = 8;
  uint32_t crc32;
  int ret;

  if (len < CRC32_STR_LEN)
    {
      ret = -EINVAL;
      goto error;
    }

  crc32 = strtoul((FAR char *) buf, NULL, 16);
  if (crc32 == 0)
    {
      ret = -EINVAL;
      goto error;
    }

  priv->fwupdate_ctx.crc = crc32;
  priv->fwupdate_ctx.fw_len = priv->fwupdate_ctx.total_len;
  priv->fwupdate_ctx.segment = buf;
  priv->fwupdate_ctx.segment_len = CRC32_STR_LEN;
  priv->fwupdate_ctx.set_crc = true;

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   NULL, NULL,
                   procret_initfwupdate,
                   "AT+FWUPDATE=%zu,0x%08X\r\n",
                   priv->fwupdate_ctx.fw_len, priv->fwupdate_ctx.crc);
error:
  if (ret < 0)
    {
      send_usock_ack(priv, ret);
    }

  return ret;
}

/****************************************************************************
 * Name: proc_firmware
 ****************************************************************************/

static void proc_firmware(FAR struct nrc7292_s *priv,
                          FAR uint8_t *buf, size_t len)
{
  int ret;

  if (!priv->fwupdate_ctx.set_crc)
    {
      init_fwupdate(priv, buf, len);
    }
  else
    {
      priv->fwupdate_ctx.segment = buf;
      priv->fwupdate_ctx.segment_len = len;

      ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                       "BEVENT", procresp_fwinject,
                       procret_fwinject,
                       "AT+FWBINDL=%zu,%zu\r\n",
                       priv->fwupdate_ctx.written_len,
                       priv->fwupdate_ctx.segment_len);

      if (ret < 0)
        {
          send_usock_ack(priv, ret);
        }
    }
}

/****************************************************************************
 * Name: proc_setfwupdate_inject
 ****************************************************************************/

static int proc_setfwupdate_inject(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  FAR uint8_t *data = iwr->u.data.pointer;
  size_t len = iwr->u.data.length;

  proc_firmware(priv, data, len);

  return OK;
}

/****************************************************************************
 * Name: procresp_reboot
 ****************************************************************************/

static int procresp_reboot(FAR struct nrc7292_s *priv,
                                  FAR struct atcmd_resp_tokens_s *resp)
{
  send_usock_ack(priv, OK);

  term_daemon(priv);

  return ATCMD_FIN;
}

/****************************************************************************
 * Name: procret_setfwupdate_exec
 ****************************************************************************/

static int procret_setfwupdate_exec(FAR struct nrc7292_s *priv, int ret)
{
  int retval;
  int atcmd_ret = ATCMD_FIN;

  if (ret == 0)
    {
      retval = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                          "BOOT", procresp_reboot,
                          NULL,
                          "ATZ\r\n");
    }
  else
    {
      retval = -EINVAL;
    }

  if (retval < 0)
    {
      send_usock_ack(priv, retval);
    }
  else
    {
      priv->suppress_crushed_event = true;
      atcmd_ret = ATCMD_FIN;
    }

  return atcmd_ret;
}

/****************************************************************************
 * Name: proc_setfwupdate_exec
 ****************************************************************************/

static int proc_setfwupdate_exec(FAR struct nrc7292_s *priv, FAR void *arg)
{
  int ret = OK;

  if (priv->fwupdate_ctx.written_len != priv->fwupdate_ctx.fw_len)
    {
      ret = -EINVAL;
      goto error;
    }

  ret = exec_atcmd(&priv->atcmd_ctx, priv->drv_fd,
                   NULL, NULL,
                   procret_setfwupdate_exec,
                   "AT+FWUPDATE\r\n");

error:
  if (ret != OK)
    {
      return send_usock_ack(priv, ret);
    }

  return OK;
}

/****************************************************************************
 * Name: proc_register_cb
 ****************************************************************************/

static int proc_register_cb(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct iwreq *iwr = (FAR struct iwreq *) arg;
  wapi_event_cb_t cb;
  int ret = OK;
  int i;

  cb = iwr->u.data.pointer;

  pthread_mutex_lock(&priv->evtcb_list_mtx);

  /* Register callback function */

  if (iwr->u.data.flags != 0)
    {
      for (i = 0; i < MAX_EVT_CB_NUM; i++)
        {
          if (priv->evtcb_list[i] == NULL
              && priv->evtcb_list[i] != cb)
            {
              priv->evtcb_list[i] = cb;
              break;
            }
        }

      if (i == MAX_EVT_CB_NUM)
        {
          ret = -ENOMEM;
        }
    }
  else
    {
      for (i = 0; i < MAX_EVT_CB_NUM; i++)
        {
          if (priv->evtcb_list[i] == cb)
            {
              priv->evtcb_list[i] = NULL;
              break;
            }
        }
    }

  pthread_mutex_unlock(&priv->evtcb_list_mtx);

  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: proc_usockdterm
 ****************************************************************************/

static int proc_usockdterm(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct ifreq *ifr;
  int ret;

  ifr = (FAR struct ifreq *) arg;

  if (strncmp(ifr->ifr_name, WLAN_IFNAME, WLAN_IFNAME_LEN) == 0)
    {
      term_daemon(priv);
      ret = OK;
    }
  else
    {
      ret = -EINVAL;
    }

  return send_usock_ack(priv, ret);
}

/****************************************************************************
 * Name: proc_startdhcp
 ****************************************************************************/

static int proc_startdhcp(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct ifreq *ifr;

  ifr = (FAR struct ifreq *) arg;

  return do_dhcp(priv, ifr->ifr_name);
}

/****************************************************************************
 * Name: proc_stopdhcp
 ****************************************************************************/

static int proc_stopdhcp(FAR struct nrc7292_s *priv, FAR void *arg)
{
  FAR struct ifreq *ifr;

  ifr = (FAR struct ifreq *) arg;

  return do_ifconfig(priv, ifr->ifr_name);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: set_ipinfo_to_driver
 ****************************************************************************/

void set_ipinfo_to_driver(FAR struct nrc7292_s *priv, bool notif)
{
  struct ifreq ifr;
  FAR struct sockaddr_in *sin;

  memset(&ifr, 0, sizeof(ifr));

  strcpy(ifr.ifr_name, WLAN_IFNAME);
  sin = (struct sockaddr_in *) &ifr.ifr_ifru;

  sin->sin_addr = priv->ifconfig.ipaddr;
  ioctl(priv->drv_fd, SIOCSIFADDR, &ifr);
  sin->sin_addr = priv->ifconfig.netmask;
  ioctl(priv->drv_fd, SIOCSIFNETMASK, &ifr);
  sin->sin_addr = priv->ifconfig.draddr;
  ioctl(priv->drv_fd, SIOCSIFDSTADDR, &ifr);

  if (notif)
    {
      notify_ipv4addr_updated_event(priv, &priv->ifconfig.ipaddr,
                                    &priv->ifconfig.netmask);
    }
}

/****************************************************************************
 * Name: clear_driver_ipaddr
 ****************************************************************************/

void clear_driver_ipaddr(FAR struct nrc7292_s *priv)
{
  bool notif;

  notif =
    priv->ifconfig.ipaddr.s_addr != 0 ||
    priv->ifconfig.netmask.s_addr != 0 ||
    priv->ifconfig.draddr.s_addr != 0;

  priv->ifconfig.ipaddr.s_addr = 0;
  priv->ifconfig.netmask.s_addr = 0;
  priv->ifconfig.draddr.s_addr = 0;
  set_ipinfo_to_driver(priv, notif);
}

/****************************************************************************
 * Name: handle_ioctl_request
 ****************************************************************************/

int handle_ioctl_request(int fd, FAR struct nrc7292_s *priv,
                         FAR struct usrsock_request_ioctl_s *req)
{
  FAR void *arg = NULL;
  ssize_t rlen;
  int ret;

  if (BUFLEN < req->arglen)
    {
      ret = -EINVAL;
      goto error;
    }

  rlen = read(fd, priv->buf, req->arglen);

  if (rlen < 0)
    {
      ret = -EINVAL;
      goto error;
    }

  init_atcmd_ctx(&priv->atcmd_ctx, fd, req->head.xid, NULL);
  arg = priv->buf;

  switch (req->cmd)
    {
    case SIOCGIFADDR:
      ret = proc_getipaddr(priv, arg);
      break;
    case SIOCSIFADDR:
      ret = proc_setipaddr(priv, arg);
      break;
    case SIOCGIFDSTADDR:
      ret = proc_getgateway(priv, arg);
      break;
    case SIOCSIFDSTADDR:
      ret = proc_setgateway(priv, arg);
      break;
    case SIOCGIFNETMASK:
      ret = proc_getnetmask(priv, arg);
      break;
    case SIOCSIFNETMASK:
      ret = proc_setnetmask(priv, arg);
      break;
    case SIOCGIFHWADDR:
      ret = proc_gethwaddr(priv, arg);
      break;

    /* case SIOCSIFHWADDR: */

    case SIOCSIFFLAGS:
      ret = proc_setifflags(priv, arg);
      break;
    case SIOCGIFFLAGS:
      ret = proc_getifflags(priv, arg);
      break;
    case SIOCADDRT:
      ret = proc_setgateway(priv, arg);
      break;
    case SIOCDELRT:
      break;
    case SIOCSIWFREQ:
      ret = proc_setiwfreq(priv, arg);
      break;
    case SIOCGIWFREQ:
      ret = proc_getiwfreq(priv, arg);
      break;
    case SIOCSIWMODE:
      ret = proc_setiwmode(priv, arg);
      break;
    case SIOCGIWMODE:
      ret = proc_getiwmode(priv, arg);
      break;

    /* case SIOCSIWSENS: */

    case SIOCGIWSENS:
      ret = proc_getsensitivity(priv, arg);
      break;

    /* case SIOCSIWRANGE:
     * case SIOCGIWRANGE:
     */

    case SIOCSIWAP:
      ret = proc_setiwap(priv, arg);
      break;
    case SIOCGIWAP:
      ret = proc_getiwap(priv, arg);
      break;
    case SIOCSIWSCAN:
      ret = proc_setiwscan(priv, arg);
      break;
    case SIOCGIWSCAN:
      ret = proc_getiwscan(priv, arg);
      break;
    case SIOCSIWESSID:
      ret = proc_setiwessid(priv, arg);
      break;
    case SIOCGIWESSID:
      ret = proc_getiwessid(priv, arg);
      break;

    /* case SIOCSIWRATE:
     * case SIOCGIWRATE:
     * case SIOCSIWRTS:
     * case SIOCGIWRTS:
     * case SIOCSIWFRAG:
     * case SIOCGIWFRAG:
     */

    case SIOCSIWTXPOW:
      ret = proc_settxpower(priv, arg);
      break;
    case SIOCGIWTXPOW:
      ret = proc_gettxpower(priv, arg);
      break;

    /* case SIOCSIWENCODE:
     * case SIOCGIWENCODE:
     */

    case SIOCSIWAUTH:
      ret = proc_setiwauth(priv, arg);
      break;
    case SIOCGIWAUTH:
      ret = proc_getiwauth(priv, arg);
      break;
    case SIOCSIWENCODEEXT:
      ret = proc_setiwencodeext(priv, arg);
      break;
    case SIOCGIWENCODEEXT:
      ret = proc_getiwencodeext(priv, arg);
      break;
    case SIOCSIWCOUNTRY:
      ret = proc_setcountry(priv, arg);
      break;
    case SIOCGIWCOUNTRY:
      ret = proc_getcountry(priv, arg);
      break;
     case SIOCSIWBANDWIDTH:
      ret = proc_setiwbandwidth(priv, arg);
      break;
    case SIOCGIWBANDWIDTH:
      ret = proc_getiwbandwidth(priv, arg);
      break;
    case SIOCSIFWUPDINIT:
      ret = proc_setfwupdate_init(priv, arg);
      break;
    case SIOCSIFWUPDINJECT:
      ret = proc_setfwupdate_inject(priv, arg);
      break;
    case SIOCSIFWUPDEXEC:
      ret = proc_setfwupdate_exec(priv, arg);
      break;
    case SIOCSIWCONNECT:
      ret = proc_connect(priv, arg);
      break;
    case SIOCSIWREGEVTCB:
      ret = proc_register_cb(priv, arg);
      break;
    case SIOCUSRSOCKDTERM:
      ret = proc_usockdterm(priv, arg);
      break;
    case SIOCDHCPV4START:
      ret = proc_startdhcp(priv, arg);
      break;
    case SIOCDHCPV4STOP:
      ret = proc_stopdhcp(priv, arg);
      break;
    default:
      goto error;
    }

  return ret;

error:
  return send_usock_ack(priv, ret);
}
