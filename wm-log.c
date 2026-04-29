#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wm-log.h"

void
logger(FILE* io, const char* fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  (void) vfprintf(io, fmt, ap);
  va_end(ap);

  if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
    (void) fputc(' ', io);
    perror(NULL);
  } else
    (void) fputc('\n', io);
}
