/****************************************************************************
 * apps/graphics/lvgl/port/lv_port_mouse.c
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
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <debug.h>
#include <nuttx/input/mouse.h>
#include "lv_port_mouse.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct mouse_obj_s
{
  int fd;
  lv_coord_t last_x;
  lv_coord_t last_y;
  lv_indev_state_t last_state;
  lv_indev_drv_t indev_drv;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mouse_read
 ****************************************************************************/

static void mouse_read(FAR lv_indev_drv_t *drv, FAR lv_indev_data_t *data)
{
  FAR struct mouse_obj_s *mouse_obj = drv->user_data;

  /* Read one sample */

  struct mouse_report_s sample;

  int nbytes = read(mouse_obj->fd, &sample,
                    sizeof(struct mouse_report_s));

  /* Handle unexpected return values */

  if (nbytes == sizeof(struct mouse_report_s))
    {
      /* Get the current x and y coordinates */

      const FAR lv_disp_drv_t *disp_drv = drv->disp->driver;
      lv_coord_t ver_max = disp_drv->ver_res - 1;
      lv_coord_t hor_max = disp_drv->hor_res - 1;

      mouse_obj->last_x += sample.x;
      mouse_obj->last_y += sample.y;
      mouse_obj->last_x = LV_CLAMP(0, mouse_obj->last_x, hor_max);
      mouse_obj->last_y = LV_CLAMP(0, mouse_obj->last_y, ver_max);

      /* Get whether the mouse button is pressed or released */

      if (sample.buttons & MOUSE_BUTTON_1)
        {
          mouse_obj->last_state = LV_INDEV_STATE_PRESSED;
        }
      else
        {
          mouse_obj->last_state = LV_INDEV_STATE_RELEASED;
        }
    }

  /* Update mouse data */

  data->point.x = mouse_obj->last_x;
  data->point.y = mouse_obj->last_y;
  data->state = mouse_obj->last_state;
}

/****************************************************************************
 * Name: mouse_init
 ****************************************************************************/

static FAR lv_indev_t *mouse_init(int fd)
{
  FAR lv_indev_t *indev_mouse;
  FAR struct mouse_obj_s *mouse_obj;
  mouse_obj = malloc(sizeof(struct mouse_obj_s));

  if (mouse_obj == NULL)
    {
      LV_LOG_ERROR("mouse_obj_s malloc failed");
      return NULL;
    }

  mouse_obj->fd = fd;
  mouse_obj->last_x = 0;
  mouse_obj->last_y = 0;
  mouse_obj->last_state = LV_INDEV_STATE_RELEASED;

  lv_indev_drv_init(&(mouse_obj->indev_drv));
  mouse_obj->indev_drv.type = LV_INDEV_TYPE_POINTER;
  mouse_obj->indev_drv.read_cb = mouse_read;
#if ( LV_USE_USER_DATA != 0 )
  mouse_obj->indev_drv.user_data = mouse_obj;
#else
#error LV_USE_USER_DATA must be enabled
#endif

  indev_mouse = lv_indev_drv_register(&(mouse_obj->indev_drv));

  /* Set cursor */

  FAR lv_obj_t *mouse_cursor = lv_img_create(lv_scr_act());
  lv_img_set_src(mouse_cursor, LV_SYMBOL_OK);
  lv_indev_set_cursor(indev_mouse, mouse_cursor);

  return indev_mouse;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lv_port_mouse_init
 *
 * Description:
 *   Mouse interface initialization.
 *
 * Input Parameters:
 *   dev_path - input device path, set to NULL to use the default path.
 *
 * Returned Value:
 *   lv_indev object address on success; NULL on failure.
 *
 ****************************************************************************/

FAR lv_indev_t *lv_port_mouse_init(FAR const char *dev_path)
{
  FAR const char *device_path = dev_path;
  FAR lv_indev_t *indev;
  int fd;

  if (device_path == NULL)
    {
      device_path = CONFIG_LV_PORT_MOUSE_DEFAULT_DEVICEPATH;
    }

  LV_LOG_INFO("mouse %s opening", device_path);
  fd = open(device_path, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      LV_LOG_ERROR("mouse %s open failed: %d", device_path, errno);
      return NULL;
    }

  LV_LOG_INFO("mouse %s open success", device_path);

  indev = mouse_init(fd);

  if (indev == NULL)
    {
      close(fd);
    }

  return indev;
}
