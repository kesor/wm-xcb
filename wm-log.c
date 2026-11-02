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

  /* A redirected log is fully buffered, and a WM is usually killed rather than
     exited, so a line that is still in the buffer never reaches the file and
     the log stops mid-line at the buffer boundary. That reads as "the event
     never arrived", which is what cost a session reading a DEBUG log. */
  (void) fflush(io);
}
