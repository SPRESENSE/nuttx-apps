/****************************************************************************
 * apps/lte/alt1250/alt1250_evt.h
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

#ifndef __APPS_LTE_ALT1250_ALT1250_EVT_H
#define __APPS_LTE_ALT1250_ALT1250_EVT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef ARRAY_SZ
#  define ARRAY_SZ(array) (sizeof(array)/sizeof(array[0]))
#endif

/* Represents when to clear the callback function */

/* Clear callbacks at all */

#define ALT1250_CLRMODE_ALL        (0)

/* Clear callbacks without restart callback */

#define ALT1250_CLRMODE_WO_RESTART (1)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int alt1250_setevtbuff(int altfd);
int alt1250_evtdestroy(void);
int alt1250_regevtcb(uint32_t id, FAR void *cb);
void alt1250_execcb(uint64_t evtbitmap);
FAR void **alt1250_getevtarg(uint32_t cmdid);
bool alt1250_checkcmdid(uint32_t cmdid, uint64_t evtbitmap,
  FAR uint64_t *bit);
void alt1250_setevtarg_writable(uint32_t cmdid);
int alt1250_clrevtcb(uint8_t mode);

#endif /* __APPS_LTE_ALT1250_ALT1250_EVT_H */
