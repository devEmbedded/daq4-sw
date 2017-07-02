#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include <libopencm3/stm32/timer.h>

void board_initialize();

/* Get microsecond timestamp */
typedef uint32_t systime_t;
#define SYSTIME_FREQ 1000000
static inline systime_t get_systime()
{
  return TIM2_CNT;
}


#endif
