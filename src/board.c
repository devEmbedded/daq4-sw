#include "board.h"
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/crs.h>

void board_initialize()
{
  /* 48 MHz HSI oscillator */
  rcc_clock_setup_in_hsi48_out_48mhz();
  
  rcc_periph_clock_enable(RCC_GPIOA);
  rcc_periph_clock_enable(RCC_GPIOB);
  rcc_periph_clock_enable(RCC_GPIOF);
  rcc_periph_clock_enable(RCC_USART2);
  rcc_periph_clock_enable(RCC_TIM2);
  rcc_periph_clock_enable(RCC_USB);

  /* USB */
  crs_autotrim_usb_enable();
  rcc_set_usbclk_source(RCC_HSI48);
  gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
  gpio_set_af(GPIOA, GPIO_AF10, GPIO11 | GPIO12);
  
  /* USART output on GPIOA2 */
  gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO2);
  gpio_set_af(GPIOA, GPIO_AF1, GPIO2);
  
  usart_set_baudrate(USART2, 115200);
  usart_set_databits(USART2, 8);
  usart_set_mode(USART2, USART_MODE_TX);
  usart_enable(USART2);
  
  /* TIM2 for systime, 1 MHz freq */
  TIM2_PSC = 47;
  TIM2_ARR = 0xFFFFFFFF;
  TIM2_CNT = 0;
  TIM2_CR1 = TIM_CR1_CEN;
  TIM2_EGR = TIM_EGR_UG;
}

