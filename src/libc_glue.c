#include <stdio.h>
#include <libopencm3/stm32/usart.h>

size_t write_usart2(FILE* instance, const char *bp, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        usart_send_blocking(USART2, *bp++);
    }
    return n;
}

static struct File_methods g_usart2_vmt = {
    .write = &write_usart2,
    .read = NULL
};

static FILE g_usart2 = {&g_usart2_vmt};

FILE* const stdout = &g_usart2;
FILE* const stderr = &g_usart2;

void __assert_fail(const char *msg, const char *file, unsigned int line)
{
  printf("%s:%d Assertion failed: %s\n", file, line, msg);
  while(1);
}
