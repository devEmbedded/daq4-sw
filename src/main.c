#include <stdlib.h>
#include <stdio.h>
#include "board.h"
#include "cdcncm.h"
#include "tcpip.h"
#include <libopencm3/stm32/st_usbfs.h>

int main(void)
{
  usbd_device *usbd_dev;
  
  board_initialize();
  
  usbd_dev = cdcncm_init(&st_usbfs_v2_usb_driver);
  
  while (1)
  {
    usbd_poll(usbd_dev);
    tcpip_poll();
  }
}

