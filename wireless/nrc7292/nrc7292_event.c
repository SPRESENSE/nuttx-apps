/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_event.c
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
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#include <sys/timerfd.h>

#include "nrc7292_daemon.h"
#include "nrc7292_event.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DHCP_RETRY_INTERVAL (60)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: set_timeout
 ****************************************************************************/

static void set_timeout(int fd, time_t second)
{
  struct itimerspec ts;
  int ret;

  memset(&ts, 0, sizeof(ts));
  ts.it_value.tv_sec = second;

  ret = timerfd_settime(fd, 0, &ts, NULL);

  if (ret < 0)
    {
      perror("timerfd_settime");
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: notify_event_to_daemon
 ****************************************************************************/

int notify_event_to_daemon(int fd, enum mon_event_e event)
{
  struct monitor_args_s args;
  int ret;

  memset(&args, 0, sizeof(args));
  args.event = event;

  ret = write(fd, &args, sizeof(args));

  if (ret < 0)
    {
      return errno;
    }

  return 0;
}

/****************************************************************************
 * Name: notify_event_to_apps
 ****************************************************************************/

int notify_event_to_apps(FAR struct nrc7292_s *priv,
                         FAR struct wapi_event_cb_args_s *args)
{
  int ret;

  if (priv == NULL || args == NULL)
    {
      return -EINVAL;
    }

  ret = write(priv->fifotx_fd, args, sizeof(*args));

  if (ret < 0)
    {
      return errno;
    }

  return 0;
}

/****************************************************************************
 * Name: daemon_event_thread
 ****************************************************************************/

FAR void *daemon_event_thread(FAR void *arg)
{
  FAR struct nrc7292_s *priv = (FAR struct nrc7292_s *) arg;
  struct wapi_event_cb_args_s evt_args;
  struct pollfd fds[2];
  int fifo_fd;
  int dhcpc_timer_fd;
  int notif_fd;
  int ret;
  int i;
  uint64_t exp;
  nfds_t nfds;

  fifo_fd = open(NRCD_FIFOTX_PATH, O_RDONLY);
  dhcpc_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  notif_fd = open(NRCD_FIFORX_PATH, O_WRONLY);

  while (true)
    {
      memset(fds, 0, sizeof(fds));

      fds[0].fd     = fifo_fd;
      fds[0].events = POLLIN;
      fds[1].fd     = dhcpc_timer_fd;
      fds[1].events = POLLIN;
      nfds = 2;

      ret = poll(fds, nfds, -1);
      ASSERT(0 < ret);

      if (fds[1].revents & POLLIN)
        {
          read(dhcpc_timer_fd, &exp, sizeof(exp));
          notify_event_to_daemon(notif_fd, MONITOR_DHCP_START);
        }

      if (fds[0].revents & POLLIN)
        {
          ret = read(fifo_fd, &evt_args, sizeof(evt_args));

          switch (evt_args.event)
            {
            case WAPI_EVENT_CONNECTED:
            case WAPI_EVENT_RELAY_ENABLED:
              set_timeout(dhcpc_timer_fd, 0);
              notify_event_to_daemon(notif_fd, MONITOR_DHCP_START);
              break;
            case WAPI_EVENT_DHCP_TIMEOUT:
              set_timeout(dhcpc_timer_fd, DHCP_RETRY_INTERVAL);
              break;
            case WAPI_EVENT_IPADDR_UPDATED:
              set_timeout(dhcpc_timer_fd, 0);
              break;
            case WAPI_EVENT_MODULE_CRUSHED:
            case WAPI_EVENT_AP_DISABLED:
            case WAPI_EVENT_RELAY_DISABLED:
              set_timeout(dhcpc_timer_fd, 0);
              notify_event_to_daemon(notif_fd, MONITOR_RESET_CTX);
              break;
            case WAPI_EVENT_DISCONNECTED:
              set_timeout(dhcpc_timer_fd, 0);
              notify_event_to_daemon(notif_fd, MONITOR_SOCKET_CLOSE);
              break;
            case WAPI_EVENT_TERMINATED:
              goto exit;
            default:
              break;
            }

          /* notify event to apps */

          pthread_mutex_lock(&priv->evtcb_list_mtx);

          for (i = 0; i < MAX_EVT_CB_NUM; i++)
            {
              if (priv->evtcb_list[i] != NULL)
                {
                  priv->evtcb_list[i](&evt_args);
                }
            }

          pthread_mutex_unlock(&priv->evtcb_list_mtx);
        }
    }

exit:
  close(fifo_fd);
  close(dhcpc_timer_fd);
  close(notif_fd);

  return NULL;
}

