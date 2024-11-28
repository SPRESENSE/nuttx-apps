/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_daemon.h
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

#ifndef __APPS_WIRELESS__NRC7292_NRC7292_DAEMON_H
#define __APPS_WIRELESS__NRC7292_NRC7292_DAEMON_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>
#include <stdint.h>
#include <debug.h>
#include <errno.h>

#include "wireless/wapi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SOCKET_COUNT 16
#define ATCMD_BUFLEN 512
#define BUFLEN       4096

#define MAX_EVT_CB_NUM 4

#define SSID_MAX_LEN        32 + 1
#define ETHADDR_LEN         6
#define PASSPHRASE_MAX_LEN  63 + 1

#define WLAN_IFNAME       "wlan0"
#define WLAN_IFNAME_LEN   5

#define RELAY_IFNAME     "relay-wlan0"
#define RELAY_IFNAME_LEN 11

/* wconfig index */

#define STA 0
#define AP  1

#define ATCMD_FIN      0
#define ATCMD_CONTINUE 1

#define NRCD_FIFOTX_PATH "/dev/nrcfifo_tx"
#define NRCD_FIFORX_PATH "/dev/nrcfifo_rx"

/* Default Values on initialization */

#define DEFAULT_BANDFREQUENCY_921KHZ  (921 * 1000)

#ifdef CONFIG_NRC7292_SPECIFIC_DEFAULT_BANDFREQUENCY
#  define DEFAULT_BANDFREQUENCY \
    CONFIG_NRC7292_SPECIFIC_DEFAULT_BANDFREQUENCY_KHZ
#else
#  define DEFAULT_BANDFREQUENCY DEFAULT_BANDFREQUENCY_921KHZ
#endif

#define NRC7282_CHANNEL_BANDWIDTH_1MHz (1000)

/* #define NRC7292_TRACE */

#ifdef NRC7292_TRACE
#  define nrc7292_printf(v, ...) printf(v, ##__VA_ARGS__)
#else
#  define nrc7292_printf(v, ...)
#endif

/****************************************************************************
 * Public Data Types
 ****************************************************************************/

/* TODO: structure names are much genelic, should be refactor. */

enum sock_state_e
  {
    DISCONNECTED,
    CONNECTED,
    LISTEN,
    ACCEPTING,
    CLOSED,
    STATE_MAX
  };

enum sock_bind_state_e
  {
    UNBOUND,
    BOUND,
    BIND_STATE_MAX
  };

enum sock_accept_state_e
  {
    NOT_ACCEPTED,
    ACCEPTED,
    ACCEPT_STATE_MAX
  };

enum scan_state_e
  {
    SCAN_STOPPED,
    SCAN_RUNNING,
    SCAN_DONE
  };

enum wireless_conn_state_e
  {
    WIRELESS_DISCONNECTED,
    WIRELESS_CONNECTED,
    WIRELESS_AP_ENABLED,
    WIRELESS_RELAY_ENABLED
  };

enum exec_mode_e
  {
    ATCMD_MODE,
    USRSOCK_MODE
  };

struct board_info_s
{
  char hwver[8];
  char manufacturer[16];
  char regdom[3];
  uint8_t power;
};

struct nrc_firm_version_s
{
  char sdk[16];
  char atcmd[16];
};

struct socket_info_s
{
  int usock_id;   /* NuttX side socket ID */
  int socket_id;  /* NRC side socket ID */
  int domain;
  int type;
  int error;
  int xid;
  int len;
  enum sock_state_e state;
  enum sock_bind_state_e bind_state;
  struct sockaddr_in remote_addr;
  struct sockaddr_in bind_addr;

  /* socket option */

  uint32_t sopt_flags;
  int so_keepalive;
  int so_reuseaddr;
  int tcp_nodelay;
};

union ioctl_resp_u
{
  struct ifreq ifr;
  struct iwreq iwr;
  struct sockaddr_in remote_addr;
  uint8_t value[32];
};

struct ifconfig_s
{
  struct in_addr ipaddr;
  struct in_addr netmask;
  struct in_addr draddr;
};

struct nrc7292_s;
struct atcmd_resp_tokens_s;
typedef FAR int (*procresp_t)(FAR struct nrc7292_s *priv,
                              FAR struct atcmd_resp_tokens_s *resp);
typedef FAR int (*procret_t)(FAR struct nrc7292_s *priv, int ret);

struct atcmd_ctx_s
{
  int fd;
  bool is_active;
  bool resp_received;
  bool ret_received;
  FAR struct socket_info_s *sinfo;
  FAR const char *hook_event;
  procresp_t procresp_cb;
  procret_t procret_cb;
  uint32_t xid;
  union ioctl_resp_u resp;
  size_t resp_len;
  struct sockaddr_in dst_addr;

  int return_error_code;
  char atcmd_txbuf[ATCMD_BUFLEN];
  char atcmd_rxbuf[ATCMD_BUFLEN];
};

struct wpa_wconfig_ext_s
{
  uint16_t bandwidth;
};

struct halow_wconfig_s
{
  struct wpa_wconfig_s conf;
  struct wpa_wconfig_ext_s ext;
};

struct fwupdate_ctx_s
{
  size_t total_len;
  size_t fw_len;
  size_t written_len;
  bool set_crc;
  uint32_t crc;

  FAR uint8_t *segment;
  size_t segment_len;
};

#define SCAN_RES_MAX_SIZE CONFIG_WIRELESS_WAPI_SCAN_MAX_DATA

struct nrc7292_s
{
  int     usock_enable; /* TODO:This is for VPN. Support in near future */
  int     fifotx_fd;    /* For communicate with callback Task */
  int     fiforx_fd;    /* For communicate with callback Task */
  int     usock_fd;
  int     drv_fd;
  char    country[3];   /* Store "JP", "US" or "EU" */
  struct nrc_firm_version_s fw_ver;
  struct board_info_s board_info;
  uint8_t macaddr[IFHWADDRLEN];

  struct socket_info_s sockets[SOCKET_COUNT];

  uint8_t buf[BUFLEN];
  size_t buf_len;
  struct atcmd_ctx_s atcmd_ctx;
  struct fwupdate_ctx_s fwupdate_ctx;
  struct ifconfig_s ifconfig;
  struct halow_wconfig_s wconfig[2];
  enum wireless_conn_state_e wireless_state;
  uint8_t bssid[IFHWADDRLEN];
  enum scan_state_e scan_state;
  struct wapi_list_s scan_res_list;
  uint8_t scan_results[SCAN_RES_MAX_SIZE];
  size_t scan_results_size;

  uint32_t buffered_size_table[SOCKET_COUNT];

  pthread_mutex_t evtcb_list_mtx;
  wapi_event_cb_t evtcb_list[MAX_EVT_CB_NUM];

  enum exec_mode_e exec_mode;
  bool to_terminate;
  bool suppress_crushed_event;
};

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

#endif /* __APPS_WIRELESS__NRC7292_NRC7292_DAEMON_H */
