/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_state.c
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
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <nuttx/net/usrsock.h>
#include <nuttx/net/ioctl.h>

#include "nrc7292_daemon.h"
#include "nrc7292_socket.h"
#include "nrc7292_ioctl.h"
#include "nrc7292_event.h"
#include "nrc7292_usockif.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: notify_ap_event
 ****************************************************************************/

static void notify_ap_event(FAR struct nrc7292_s *priv,
                            bool enabled)
{
  struct wapi_event_cb_args_s arg;

  memset(&arg, 0, sizeof(arg));
  arg.event
    = enabled ? WAPI_EVENT_AP_ENABLED : WAPI_EVENT_AP_DISABLED;

  notify_event_to_apps(priv, &arg);
}

/****************************************************************************
 * Name: notify_relay_event
 ****************************************************************************/

static void notify_relay_event(FAR struct nrc7292_s *priv,
                               bool enabled)
{
  struct wapi_event_cb_args_s arg;

  memset(&arg, 0, sizeof(arg));
  arg.event
    = enabled ? WAPI_EVENT_RELAY_ENABLED : WAPI_EVENT_RELAY_DISABLED;

  notify_event_to_apps(priv, &arg);
}

/****************************************************************************
 * Name: notify_connection_event
 ****************************************************************************/

static void notify_connection_event(FAR struct nrc7292_s *priv,
                                    bool enabled)
{
  struct wapi_event_cb_args_s arg;
  FAR struct wapi_event_conn_s *conn = &arg.u.conn;

  memset(&arg, 0, sizeof(arg));
  arg.event = enabled ? WAPI_EVENT_CONNECTED : WAPI_EVENT_DISCONNECTED;

  strncpy(conn->essid, priv->wconfig[STA].conf.ssid, 32);
  memcpy(conn->bssid, priv->bssid, IFHWADDRLEN);
  conn->auth = priv->wconfig[STA].conf.auth_wpa;

  if (conn->auth == IW_AUTH_WPA_VERSION_DISABLED)
    {
      conn->cipher = IW_AUTH_CIPHER_NONE;
    }
  else
    {
      conn->cipher = IW_AUTH_CIPHER_CCMP;

      if (priv->wconfig[STA].conf.phraselen == 0)
        {
          conn->key_mgmt = IW_AUTH_KEY_MGMT_OWE;
        }
      else
        {
          conn->key_mgmt = IW_AUTH_KEY_MGMT_PSK;
        }
    }

  write(priv->fifotx_fd, &arg, sizeof(arg));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: update_wireless_state
 ****************************************************************************/

void update_wireless_state(FAR struct nrc7292_s *priv,
                           enum wireless_conn_state_e state)
{
  struct ifreq ifr;

  memset(&ifr, 0, sizeof(ifr));

  if (priv->wireless_state == state)
    {
      return;
    }

  switch (state)
    {
    case WIRELESS_CONNECTED:
      {
        ifr.ifr_flags = IFF_UP;
        strcpy(ifr.ifr_name, WLAN_IFNAME);
        ioctl(priv->drv_fd, SIOCSIFFLAGS, &ifr);
        notify_connection_event(priv, state);
      }
      break;

    case WIRELESS_DISCONNECTED:
      {
        memset(priv->bssid, 0, sizeof(priv->bssid));
        ifr.ifr_flags = IFF_DOWN;
        strcpy(ifr.ifr_name, WLAN_IFNAME);
        ioctl(priv->drv_fd, SIOCSIFFLAGS, &ifr);

        if (priv->wireless_state == WIRELESS_CONNECTED)
          {
            notify_connection_event(priv, state);
          }
        else if (priv->wireless_state == WIRELESS_AP_ENABLED)
          {
            notify_ap_event(priv, false);
          }
        else if (priv->wireless_state == WIRELESS_RELAY_ENABLED)
          {
            notify_relay_event(priv, false);
          }
      }
      break;

    case WIRELESS_AP_ENABLED:
      {
        ifr.ifr_flags = IFF_UP;
        strcpy(ifr.ifr_name, WLAN_IFNAME);
        ioctl(priv->drv_fd, SIOCSIFFLAGS, &ifr);
        notify_ap_event(priv, true);
      }
      break;

    case WIRELESS_RELAY_ENABLED:
      {
        ifr.ifr_flags = IFF_UP;
        strcpy(ifr.ifr_name, WLAN_IFNAME);
        ioctl(priv->drv_fd, SIOCSIFFLAGS, &ifr);
        notify_relay_event(priv, true);
      }
      break;

    default:
      break;
    }

  priv->wireless_state = state;
}

/****************************************************************************
 * Name: allocate_client_usock_socket
 ****************************************************************************/

static FAR struct socket_info_s *
allocate_client_usock_socket(FAR struct nrc7292_s *priv,
                             int socket_id,
                             FAR const char *ip_str,
                             uint16_t rport,
                             uint16_t lport)
{
  FAR struct socket_info_s *sinfo = NULL;
  FAR struct socket_info_s *srv_sinfo;
  int16_t usockid;
  uint16_t events;
  uint32_t buffered_size;
  int ret;

  srv_sinfo = nrc7292_socket_get_by_localport(priv, lport);

  if (srv_sinfo == NULL)
    {
      return NULL;
    }

  if (srv_sinfo->state != ACCEPTING)
    {
      send_usock_socket_events(priv->usock_fd, USRSOCK_EVENT_RECVFROM_AVAIL,
                               srv_sinfo->usock_id);
      return NULL;
    }

  usockid = nrc7292_socket_alloc(priv, AF_INET, SOCK_STREAM);

  if (usockid < 0)
    {
      send_usock_ack_nodata(priv->usock_fd, srv_sinfo->xid, -EMFILE);
      return NULL;
    }

  sinfo = nrc7292_socket_get(priv, usockid);
  sinfo->socket_id = socket_id;
  sinfo->state = CONNECTED;
  sinfo->remote_addr.sin_family = AF_INET;
  sinfo->remote_addr.sin_port = htons(rport);
  inet_aton(ip_str, &sinfo->remote_addr.sin_addr);

  ret = send_usock_data_ack_with_addr(priv->usock_fd,
                                      srv_sinfo->xid,
                                      2,  /* addr + usock */
                                      (FAR uint8_t *) &sinfo->remote_addr,
                                      sizeof(sinfo->remote_addr),
                                      (FAR uint8_t *) &usockid,
                                      sizeof(usockid));
  if (ret == 0)
    {
      events = USRSOCK_EVENT_SENDTO_READY;
      buffered_size = get_buffered_packet_size(priv, socket_id);

      if (0 < buffered_size)
        {
          events |= USRSOCK_EVENT_RECVFROM_AVAIL;
        }

      send_usock_socket_events(priv->usock_fd, events,
                               usockid);
      srv_sinfo->state = LISTEN;
    }

  return sinfo;
}

/****************************************************************************
 * Name: update_socket_state
 ****************************************************************************/

void update_socket_state(FAR struct nrc7292_s *priv, int socket_id,
                         enum sock_state_e next_state,
                         int argc, FAR char *argv[])
{
  FAR struct socket_info_s *sinfo;
  int16_t events;
  enum sock_state_e cur_state;

  sinfo = nrc7292_socket_get_by_socketid(priv, socket_id);

  cur_state = (sinfo == NULL) ? CLOSED : sinfo->state;

  nrc7292_printf("[%s] socket_id:%d(%d -> %d)\n",
                 __FUNCTION__, socket_id, cur_state, next_state);

  switch (cur_state)
    {
    case DISCONNECTED:
      if (next_state == CLOSED)
        {
          events = USRSOCK_EVENT_ABORT;
          send_usock_socket_events(priv->usock_fd,
                                   events, sinfo->usock_id);
        }
      break;

    case CONNECTED:
      if (next_state == DISCONNECTED || next_state == CLOSED)
        {
          events = USRSOCK_EVENT_ABORT;
          send_usock_socket_events(priv->usock_fd,
                                   events, sinfo->usock_id);
        }
      break;

    case LISTEN:
      if (next_state == CLOSED)
        {
          events = USRSOCK_EVENT_ABORT;
          send_usock_socket_events(priv->usock_fd,
                                   events, sinfo->usock_id);
        }
      break;

    case ACCEPTING:
      if (next_state == CLOSED)
        {
          events = USRSOCK_EVENT_ABORT;
          send_usock_socket_events(priv->usock_fd,
                                   events, sinfo->usock_id);
        }
      break;

    case CLOSED:
      if (next_state == CONNECTED)
        {
          /* accepting client */

          if (sinfo == NULL && 2 < argc)
            {
              FAR char *ip;
              int rport;
              int lport;

              ip = argv[0];
              rport = strtol(argv[1], NULL, 10);
              lport = strtol(argv[2], NULL, 10);
              sinfo = allocate_client_usock_socket(priv, socket_id,
                                                   ip, rport, lport);
            }
        }
      break;

    default:
      break;
    }

  if (sinfo != NULL)
    {
      sinfo->state = next_state;
    }

  if (next_state == CLOSED)
    {
      nrc7292_socket_free(priv, sinfo);
    }
}

/****************************************************************************
 * Name: update_socket_state
 ****************************************************************************/

void update_bind_state(FAR struct socket_info_s *sinfo,
                       enum sock_bind_state_e next_state)
{
  if (sinfo != NULL)
    {
      sinfo->bind_state = next_state;
    }
}

/****************************************************************************
 * Name: update_socket_state
 ****************************************************************************/

void update_scan_state(FAR struct nrc7292_s *priv,
                       enum scan_state_e next_state)
{
  if (priv == NULL)
    {
      return;
    }

  switch (priv->scan_state)
    {
    case SCAN_STOPPED:
    case SCAN_DONE:
      if (next_state == SCAN_RUNNING)
        {
          /* clear scan results */

          priv->scan_results_size = 0;
        }
      break;

    case SCAN_RUNNING:
      break;

    default:
      break;
    }

  priv->scan_state = next_state;
}

