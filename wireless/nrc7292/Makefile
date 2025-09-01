############################################################################
# apps/wireless/nrc7292/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# nrc7292 command

PROGNAME = $(CONFIG_WIRELESS_NRC7292_PROGNAME)
PRIORITY = $(CONFIG_WIRELESS_NRC7292_PRIORITY)
STACKSIZE = $(CONFIG_WIRELESS_NRC7292_STACKSIZE)
MODULE = $(CONFIG_WIRELESS_NRC7292)

# Files

CSRCS += nrc7292_socket.c
CSRCS += nrc7292_ioctl.c
CSRCS += nrc7292_usockif.c
CSRCS += nrc7292_atcmd.c
CSRCS += nrc7292_state.c
CSRCS += nrc7292_regdom.c
CSRCS += nrc7292_event.c

MAINSRC = nrc7292_main.c

include $(APPDIR)/Application.mk
