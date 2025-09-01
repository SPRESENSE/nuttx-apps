/****************************************************************************
 * apps/wireless/nrc7292/nrc7292_regdom.c
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
 * Pre-processor Definitions
 ****************************************************************************/

#define NUM_RANGES(ranges) \
  (sizeof(ranges) / sizeof(struct regdom_freq_range))

#define RANGES(country) \
  {#country, NUM_RANGES(country), country}

/****************************************************************************
 * Public Data Types
 ****************************************************************************/

struct regdom_freq_range
{
  uint32_t start_freq;
  uint32_t end_freq;
  uint32_t bandwidth;
};

struct regdom_country
{
  FAR char *country;
  uint8_t num_ranges;
  FAR struct regdom_freq_range *ranges;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CHECK_ENABLE

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_US_ENABLE
static struct regdom_freq_range
US[] =
{
  {902500, 927500, 1000},
  {903000, 927000, 2000},
  {906000, 926000, 4000},
};
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_US_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_KR_ENABLE
static struct regdom_freq_range
K1[] =
{
  {921500, 922500, 1000},
};

static struct regdom_freq_range
K2[] =
{
  {925500, 930500, 1000},
  {927000, 929000, 2000},
};
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_KR_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_JP_ENABLE
static struct regdom_freq_range
JP[] =
{
  {921000, 927000, 1000},
  {923500, 926500, 2000},
  {924500, 925500, 4000},
};
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_JP_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_TW_ENABLE
static struct regdom_freq_range
TW[] =
{
  {839000, 851000, 1000},
  {839500, 849500, 2000},
  {840500, 848500, 4000},
};
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_TW_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_EU_ENABLE
static struct regdom_freq_range
EU[] =
{
  {863500, 867500, 1000},
};
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_EU_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CN_ENABLE
static struct regdom_freq_range
CN[] =
{
  {748500, 756500, 1000},
  {750000, 756000, 2000},
  {751000, 755500, 4000},
};
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CN_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_NZ_ENABLE
static struct regdom_freq_range
NZ[] =
{
  {915500, 927500, 1000},
  {917000, 927000, 2000},
  {918000, 926000, 4000},
};
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_NZ_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_AU_ENABLE
static struct regdom_freq_range
AU[] =
{
  {915500, 927500, 1000},
  {917000, 927000, 2000},
  {918000, 926000, 4000},
};
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_AU_ENABLE */

static struct regdom_country
g_regdom_table[] =
{
#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_US_ENABLE
  RANGES(US),
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_US_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_KR_ENABLE
  RANGES(K1),
  RANGES(K2),
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_KR_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_JP_ENABLE
  RANGES(JP),
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_JP_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_TW_ENABLE
  RANGES(TW),
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_TW_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_EU_ENABLE
  RANGES(EU),
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_EU_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CN_ENABLE
  RANGES(CN),
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CN_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_NZ_ENABLE
  RANGES(NZ),
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_NZ_ENABLE */

#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_AU_ENABLE
  RANGES(AU),
#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_AU_ENABLE */
  {NULL, 0, NULL}
};

#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CHECK_ENABLE */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: is_valid_country
 ****************************************************************************/

bool is_valid_country(FAR const char *country,
                      FAR struct board_info_s *info)
{
#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CHECK_ENABLE

  int i = 0;
  bool is_valid = false;

  if (country == NULL || info == NULL)
    {
      return false;
    }

  if (strncmp(country, info->regdom, 2) != 0)
    {
      return false;
    }

  for (i = 0; g_regdom_table[i].country != NULL; i++)
    {
      if (strncmp(country, g_regdom_table[i].country, 2) == 0)
        {
          is_valid = true;
          break;
        }
    }

  return is_valid;

#else

  return true;

#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CHECK_ENABLE */
}

/****************************************************************************
 * Name: is_valid_freq
 ****************************************************************************/

bool is_valid_freq(FAR const char *country,
                   uint32_t freq, uint32_t bandwidth)
{
#ifdef CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CHECK_ENABLE

  bool is_valid = false;
  uint32_t sfreq;
  uint32_t efreq;
  uint32_t bw;
  int i;
  int j;

  if (country == NULL)
    {
      return false;
    }

  for (i = 0; g_regdom_table[i].country != NULL; i++)
    {
      FAR struct regdom_country *regdom = &g_regdom_table[i];

      if (strncmp(country, regdom->country, 2) != 0)
        {
          continue;
        }

      for (j = 0; j < regdom->num_ranges; j++)
        {
          sfreq = regdom->ranges[j].start_freq;
          efreq = regdom->ranges[j].end_freq;
          bw    = regdom->ranges[j].bandwidth;

          if ((sfreq <= freq && freq <= efreq)
              && (bw == bandwidth))
            {
              is_valid = true;
              goto exit;
            }
        }
    }

exit:
  return is_valid;

#else

  return true;

#endif /* CONFIG_WIRELESS_NRC7292_HALOW_REGDOM_CHECK_ENABLE */
}

