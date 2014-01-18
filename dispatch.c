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

#include <string.h>

#include <fcgiapp.h>

#include "uri.h"
#include "debug.h"

#ifdef ENABLE_CGROUPS
#include "cgroups.h"
#endif

void
dispatch (FCGX_Request * request)
{
  char *uri_tail;
  char *uri = FCGX_GetParam ("REQUEST_URI", request->envp);

  debug ("request uri = `%s'", uri);

  FCGX_PutS ("Content-type: application/json\r\n", request->out);
  FCGX_PutS ("\r\n", request->out);

  if (strstr (uri, uri_prefix) != uri)
    {
      FCGX_FPrintF (request->out, "{error: \"Request must start with %s\"}",
                    uri_prefix);
      return;
    }
  else
    uri += uri_prefix_len;
  debug ("stripped uri = `%s'", uri);

  const char *driver = strtok_r (uri, "/", &uri_tail);
  debug ("driver = `%s'", driver);

  if (NULL == driver)
    FCGX_PutS ("{}", request->out);
#ifdef ENABLE_CGROUPS
  else if (0 == strcmp ("cgroups", driver))
    fcgi_cgroups (request, &uri_tail);
#endif
  else
    {
      debug ("unknown request: `%s'", driver);
      FCGX_FPrintF (request->out, "{error: \"Unknown request: %s\"}", driver);
    }
}
