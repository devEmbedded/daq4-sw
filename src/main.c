#include <stdlib.h>
#include <stdio.h>
#include "board.h"
#include "usbnet.h"
#include "tcpip.h"
#include "tcpip_diagnostics.h"
#include <libopencm3/stm32/st_usbfs.h>

int main(void)
{
  usbd_device *usbd_dev;
  
  board_initialize();
  
  printf("Boot!\n");
  
  usbd_dev = usbnet_init(&st_usbfs_v2_usb_driver, 0xD4000001);
  
  tcpip_diagnostics_init();
  
  while (1)
  {
    usbd_poll(usbd_dev);
    tcpip_poll();
  }
}

