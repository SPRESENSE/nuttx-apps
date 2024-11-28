/****************************************************************************
 * apps/examples/nrc7292_fwupdate/nrc7292_fwupdate_main.c
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include <wireless/wapi.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CHUNK_SIZE       (508 * 8)
#define CRC32_STR_MAXLEN (8)
#define USOCK_IFNAME     "wlan0"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t _buffer[CHUNK_SIZE];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: get_file_size
 ****************************************************************************/

static ssize_t get_file_size(const char *filename)
{
  struct stat st;
  ssize_t fw_size;
  int ret;

  ret = stat(filename, &st);

  if (ret < 0)
    {
      return -1;
    }

  fw_size = (ssize_t) st.st_size;

  return fw_size < 0 ? -1 : fw_size;
}

/****************************************************************************
 * Name: is_crc32_hex_string
 ****************************************************************************/

int is_crc32_hex_string(FAR const char *str)
{
  if (str == NULL)
    {
      return -1;
    }

  if (CRC32_STR_MAXLEN < strlen(str))
    {
      return -1;
    }

  while (*str)
    {
      if (!isxdigit((unsigned char) *str))
        {
          return -1;
        }

      str++;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FILE *fp = NULL;
  int fd = -1;
  ssize_t fw_size;
  size_t bytes_read = 0;
  int ret = -1;
  char crc32_str[CRC32_STR_MAXLEN];

  printf("Start NRC7292 Firmware update!!\n");

  if (argc < 3)
    {
      fprintf(stderr, "Usage: %s <file_name> <CRC32>\n", argv[0]);
      return 0;
    }

  fw_size = get_file_size(argv[1]);

  if (fw_size < 0)
    {
      fprintf(stderr, "Failed to get firmware size\n");
      goto error;
    }

  if (is_crc32_hex_string(argv[2]))
    {
      fprintf(stderr, "Invalid CRC32 format\n");
      goto error;
    }

  fp = fopen(argv[1], "rb");

  if (fp < 0)
    {
      perror("Error opening file");
      goto error;
    }

  memset(crc32_str, '\0', sizeof(crc32_str));
  strncpy(crc32_str, argv[2], sizeof(crc32_str));

  fd = wapi_make_socket();

  if (fd < 0)
    {
      perror("socket open error");
      goto error;
    }

  ret = wapi_fw_update_init(fd, USOCK_IFNAME, fw_size);
  if (ret < 0)
    {
      perror("wapi_fw_udpate_init error");
      goto error;
    }

  ret = wapi_fw_update_inject(fd, USOCK_IFNAME, crc32_str, CRC32_STR_MAXLEN);
  if (ret < 0)
    {
      perror("wapi_fw_udpate_inject error");
      goto error;
    }

  while ((bytes_read = fread(_buffer, 1, CHUNK_SIZE, fp)) > 0)
    {
      int offset = 0;

      while (offset < bytes_read)
        {
          ret = wapi_fw_update_inject(fd, USOCK_IFNAME, &_buffer[offset],
                                      bytes_read - offset);

          if (ret < 0)
            {
              perror("wapi_fw_udpate_inject error");
              goto error;
            }

          offset += ret;
        }
    }

  ret = wapi_fw_update_exec(fd, USOCK_IFNAME);

  if (ret < 0)
    {
      perror("wapi_fw_udpate_exec error");
    }

error:
  if (ret < 0)
    {
      printf("NRC7292 firmware update failed!\n");
    }
  else
    {
      printf("NRC7292 firmware updated!\n");
    }

  if (fp != NULL)
    {
      fclose(fp);
    }

  if (0 <= fd)
    {
      close(fd);
    }

  return 1;
}
