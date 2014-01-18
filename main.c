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

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcgiapp.h>

#ifdef ENABLE_CGROUPS
#include <libcgroup.h>
#endif

#include "dispatch.h"
#include "uri.h"
#include "debug.h"

static const char *progname;

/* Tunable parameters: */
static int number_of_workers = 5;
static const char *socket_path = ":9000";
static int backlog = 16;

static pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t *pthread_ids = NULL;
static int socket = -1;


static void *
worker (void *param)
{
  FCGX_Request request;

  debug ("thread #%" PRIdPTR " started", (intptr_t) param);
  if (0 != FCGX_InitRequest (&request, socket, /* int flags */ 0))
    {
      return (NULL);
    }

  while (1)
    {
      pthread_mutex_lock (&accept_mutex);
      int rc = FCGX_Accept_r (&request);
      pthread_mutex_unlock (&accept_mutex);
      debug ("thread #%" PRIdPTR " accepted request", (intptr_t) param);

      if (rc < 0)
        {
          debug ("thread #%" PRIdPTR " FCGX_Accept_r() failed: %s",
                 (intptr_t) param, strerror (errno));
          break;
        }

      dispatch (&request);

      FCGX_Finish_r (&request);
    }
  return NULL;
}


static void
version (void)
{
  printf (PACKAGE " " VERSION "\n");
#ifdef HAVE_XXD
  extern unsigned char LICENSE[];
  extern unsigned int LICENSE_len;
  printf ("%.*s\n", LICENSE_len, LICENSE);
#endif
  printf ("Report bugs to <" PACKAGE_BUGREPORT ">\n");
  exit (0);
}


static void
usage (void)
{
  printf ("Usage: %s [options]\n", progname);
  printf ("FastCGI REST server\n\n");
  printf
    ("Mandatory arguments to long options are mandatory for short options too.\n");
  printf ("  -s, --socket={path|:port}  a socket or a port number (%s)\n",
          socket_path);
  printf ("  -b, --backlog=number       listen queue depth (%d)\n", backlog);
  printf ("  -w, --threads=number       number of threads to run (%d)\n",
          number_of_workers);
  printf ("  -u, --uri-prefix=string    URI prefix to trim (%s)\n",
          uri_prefix);
  printf ("  -h, --help                 show this help message\n");
  printf ("  -v, --version              show version\n");
  exit (0);
}


static void
parse_options (int argc, char **argv)
{
  static const char *short_options = "s:b:w:u:hv";

  static const struct option long_options[] = {
    {"socket", required_argument, NULL, 's'},
    {"backlog", required_argument, NULL, 'b'},
    {"threads", required_argument, NULL, 'w'},
    {"uri-prefix", required_argument, NULL, 'u'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
  };

  progname = argv[0];

  int option_index;
  int opt;
  while ((opt = getopt_long (argc, argv, short_options,
                             long_options, &option_index)) != -1)
    switch (opt)
      {
      case 's':
        socket_path = optarg;
        break;
      case 'b':
        backlog = atoi (optarg);
        if (backlog <= 0)
          {
            fprintf (stderr,
                     "%s: backlog number must be a positive integer\n",
                     progname);
            exit (1);
          }
        break;
      case 'w':
        number_of_workers = atoi (optarg);
        if (number_of_workers <= 0)
          {
            fprintf (stderr,
                     "%s: number of workers must be a positive integer\n",
                     progname);
            exit (1);
          }
        break;
      case 'u':
        uri_prefix = optarg;
        uri_prefix_len = strlen (uri_prefix);
        break;
      case 'h':
        usage ();
        break;
      case 'v':
        version ();
        break;
      default:
        fprintf (stderr, "Use `%s --help' to get help\n", progname);
        exit (1);
        break;
      }
}


static void
init_libraries (void)
{
  int rc;
  debug ("initializing libfcgi");
  if (0 != (rc = FCGX_Init ()))
    {
      fprintf (stderr, "%s: FCGX_Init() failed. Exiting.\n", progname);
      exit (EXIT_FAILURE);
    }
#ifdef ENABLE_CGROUPS
  debug ("initializing libcgroup");
  if (0 != (rc = cgroup_init ()))
    {
      fprintf (stderr, "%s: cgroup_init() failed. Exiting.\n", progname);
      exit (EXIT_FAILURE);
    }
#endif
}


int
main (int argc, char **argv)
{
  parse_options (argc, argv);

  init_libraries ();

  fprintf (stderr,
           "%s: socket `%s', backlog %d, %d worker%s, URI prefix `%s'\n",
           progname, socket_path, backlog, number_of_workers,
           (number_of_workers == 1 ? "" : "s"), uri_prefix);

  socket = FCGX_OpenSocket (socket_path, backlog);
  if (socket < 0)
    {
      fprintf (stderr, "%s: FCGX_OpenSocket() failed: %s. Exiting.\n",
               progname, strerror (errno));
      return (EXIT_FAILURE);
    }

  debug ("allocating space for %d threads", number_of_workers);
  pthread_ids = (pthread_t *) malloc (sizeof (pthread_t) * number_of_workers);
  if (NULL == pthread_ids)
    {
      fprintf (stderr, "%s: malloc() failed: %s. Exiting.\n", progname,
               strerror (errno));
      return (EXIT_FAILURE);
    }

  debug ("starting threads");
  for (int thr = 0; thr < number_of_workers; ++thr)
    {
      int rc;
      do
        {
          debug ("starting thread #%d", thr);
          errno = 0;
          rc =
            pthread_create (&(pthread_ids[thr]), NULL, worker,
                            (void *) ((intptr_t) thr));
        }
      while ((0 != rc) && (EAGAIN == errno));

      if (0 != rc)
        {
          fprintf (stderr, "%s: pthread_create() failed: %s. Exiting.\n",
                   progname, strerror (errno));
          return (EXIT_FAILURE);
        }
    }

  for (int thr = 0; thr < number_of_workers; ++thr)
    {
      int rc = pthread_join (pthread_ids[thr], NULL);
      if (0 != rc)
        {
          fprintf (stderr, "%s: pthread_join() failed: %s. Exiting.\n",
                   progname, strerror (errno));
          return (EXIT_FAILURE);
        }
    }

  return (EXIT_SUCCESS);
}
