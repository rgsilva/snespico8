/* Calypsi libc low-level stubs: fd writes go to the test mailbox, all
** file operations fail cleanly (no filesystem on a SNES cart). */
#include <stddef.h>
#include <stdio.h>
#include <calypsi/stubs.h>
#include "calypsi/mailbox.h"

size_t _Stub_write(int fd, const void *buf, size_t count)
{
  (void)fd;
  const char *p = (const char *)buf;
  size_t i;
  for (i = 0; i < count; i++)
    mb_putc(p[i]);
  return count;
}

int _Stub_open(const char *path, int oflag, ...) { (void)path; (void)oflag; return -1; }
int _Stub_close(int fd) { (void)fd; return -1; }
long _Stub_lseek(int fd, long offset, int whence) { (void)fd; (void)offset; (void)whence; return -1; }
int _Stub_fgetpos(int fd, fpos_t *pos) { (void)fd; (void)pos; return -1; }
int _Stub_fsetpos(int fd, const fpos_t *pos) { (void)fd; (void)pos; return -1; }
size_t _Stub_read(int fd, void *buf, size_t count) { (void)fd; (void)buf; (void)count; return 0; }
int _Stub_rename(const char *o, const char *n) { (void)o; (void)n; return -1; }
int _Stub_remove(const char *path) { (void)path; return -1; }

void _Stub_exit(int exitCode)
{
  mb_finish(exitCode == 0 ? MB_STATUS_PASS : MB_STATUS_PANIC);
  for (;;)
    ;
}

char **_Stub_environ(void)
{
  static char *empty[] = { 0 };
  return empty;
}

void _Stub_assert(const char *filename, int linenum)
{
  mb_puts("ASSERT ");
  mb_puts(filename);
  mb_putc(':');
  mb_puthex32((unsigned long)linenum);
  mb_putc('\n');
  mb_finish(MB_STATUS_PANIC);
  for (;;)
    ;
}
