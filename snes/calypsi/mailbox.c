#include "mailbox.h"

void mb_init(void)
{
  MB_WINDEX = 0;
  MB_IN_WINDEX = 0;
  MB_IN_RINDEX = 0;
  MB_FLIPS = 0;
  MB_VMOPS = 0;   /* lvm.c counts straight into this slot (P8_VMOPS_ADDR) */
  MB_STATUS = MB_STATUS_RUNNING;
  MB_MAGIC = MB_MAGIC_ALIVE;
}

int mb_getc_nonblock(void)
{
  uint16_t ri = MB_IN_RINDEX;
  uint8_t b;
  if (ri == MB_IN_WINDEX) {
    return -1;
  }
  b = MB_IN_RING[ri % MB_IN_RING_SIZE];
  MB_IN_RINDEX = (uint16_t)(ri + 1);
  return b;
}

char mb_getc(void)
{
  int c;
  while ((c = mb_getc_nonblock()) < 0) {
  }
  return (char)c;
}

void mb_putc(char c)
{
  uint16_t wi = MB_WINDEX;
  MB_RING[wi % MB_RING_SIZE] = (uint8_t)c;
  MB_WINDEX = wi + 1;
}

void mb_puts(const char *s)
{
  while (*s) {
    mb_putc(*s++);
  }
}

void mb_puthex32(uint32_t v)
{
  static const char digits[] = "0123456789abcdef";
  int i;
  for (i = 28; i >= 0; i -= 4) {
    mb_putc(digits[(v >> i) & 0xf]);
  }
}

void mb_finish(uint16_t status)
{
  MB_STATUS = status;
  for (;;) {
  }
}
