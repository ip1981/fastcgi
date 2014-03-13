/* 
Copyright (c) 2014 Igor Pashev <pashev.igor@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void
__debug (const char *file, int line, const char *func, const char *msg, ...)
{
  va_list ap;
  time_t ltime;
  char time_str[32];
  char buf[512];
  int bytes = 0;

  ltime = time (NULL);
  strftime (time_str, sizeof (time_str), "%Y-%m-%d %T %z",
            localtime (&ltime));


  bytes =
    snprintf (buf, sizeof (buf), "%s %s:%d (%s): ", time_str, file, line,
              func);

  if (bytes < sizeof (buf))
    {
      va_start (ap, msg);
      bytes += vsnprintf (buf + bytes, sizeof (buf) - bytes, msg, ap);
      va_end (ap);
    }

  if (bytes < sizeof (buf))
    bytes += snprintf (buf + bytes, sizeof (buf) - bytes, "\n");
  else
    strcpy (buf + sizeof (buf) - 5, "...\n");

  (void) write (2, buf, strlen (buf));

}
