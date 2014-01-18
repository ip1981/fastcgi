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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <fcgiapp.h>
#include <libcgroup.h>

#include "debug.h"


// trim traling slashes, but not if str == "/"
static void
trim_trailing_slashes (char *str)
{
  if (NULL != str)
    {
      size_t len = strlen (str);
      if (len > 1)
        {
          char *last_symbol = (char *) str + len - 1;
          while ('/' == *last_symbol)
            last_symbol--;
          last_symbol[1] = '\0';
        }
    }
}


// Is name in "name1,name2,name3" ?
static bool
controller_is_in_list (const char *controllers, const char *name)
{
  if ((NULL == controllers) || ('\0' == controllers[0])
      || ('*' == controllers[0]))
    return true;

  const char *p = strstr (controllers, name);

  if (NULL == p)
    return false;

  if ((controllers != p) && (',' != p[-1]))
    return false;

  p += strlen (name);
  if ((',' != *p) && ('\0' != *p))
    return false;

  return true;
}


//  "name1,name2" => 2
//  "name1,name2,name3" => 3
static int
count_controllers (const char *controllers)
{
  size_t number_of_controllers = 0;
  size_t controllers_len = strlen (controllers) + 1;
  char *tail = NULL;
  char controllers_copy[controllers_len];
  memcpy (controllers_copy, controllers, controllers_len);

  char *next_controller = strtok_r (controllers_copy, ",", &tail);
  do
    {
      debug ("next controller `%s'", next_controller);
      number_of_controllers++;
      next_controller = strtok_r (NULL, ",", &tail);
    }
  while (NULL != next_controller);
  debug ("number of controllers in `%s': %d", controllers,
         number_of_controllers);
  return number_of_controllers;
}


static bool
group_has_controller (const char *controller, const char *group)
{
  int rc;
  void *handle = NULL;
  int base_level = 0;
  struct cgroup_file_info info;

  rc =
    cgroup_walk_tree_begin (controller, group, 0, &handle, &info,
                            &base_level);

  if (0 == rc)
    {
      cgroup_walk_tree_end (&handle);
      if (CGROUP_FILE_TYPE_DIR == info.type)
        return true;
    }

  return false;
}


static bool
group_exists (const char *controllers, const char *group)
{
  size_t controllers_len = strlen (controllers) + 1;
  char *tail = NULL;
  char controllers_copy[controllers_len];
  memcpy (controllers_copy, controllers, controllers_len);

  char *controller = strtok_r (controllers_copy, ",", &tail);
  do
    {
      debug ("controller `%s'", controller);
      if (!group_has_controller (controller, group))
        return false;
      controller = strtok_r (NULL, ",", &tail);
    }
  while (NULL != controller);
  return true;
}


static bool
group_has_pid (const char *controllers, const char *group, pid_t pid)
{
  bool ret = true;

  char proc_cgroup_path[sizeof ("/proc/2147483647/cgroup")];
  char pid_controllers[FILENAME_MAX];

  snprintf (proc_cgroup_path, sizeof (proc_cgroup_path), "/proc/%d/cgroup",
            pid);
  debug ("reading `%s'", proc_cgroup_path);

  FILE *proc_cgroup_file = fopen (proc_cgroup_path, "r");
  if (NULL != proc_cgroup_file)
    {
      char *next_controller = pid_controllers;
      while (!feof (proc_cgroup_file))
        {
          char cntrls[FILENAME_MAX];
          char path[FILENAME_MAX];
          int rc = fscanf (proc_cgroup_file, "%*d:%[^:]:%s\n", cntrls, path);
          if (2 != rc)
            {
              debug ("fscanf() returned %d != 2", rc);
              continue;
            }

          debug ("pid %d is in `%s:%s'", pid, cntrls, path);
          if (0 == strcmp (path, group))
            {
              if (next_controller != pid_controllers)
                {
                  *next_controller = ',';
                  next_controller++;
                }
              size_t l = strlen (cntrls);
              memcpy (next_controller, cntrls, l);
              next_controller += l;
            }
        }
      fclose (proc_cgroup_file);

      *next_controller = '\0';
      debug ("all pid %d controllers `%s'", pid, pid_controllers);

      // all controllers must be in pid_controllers:
      size_t l = strlen (controllers) + 1;
      char controllers_copy[l];
      char *tail = NULL;
      memcpy (controllers_copy, controllers, l);
      next_controller = strtok_r (controllers_copy, ",", &tail);
      while (NULL != next_controller)
        {
          debug ("checking controller `%s'", next_controller);
          if (!controller_is_in_list (pid_controllers, next_controller))
            {
              ret = false;
              break;
            }
          next_controller = strtok_r (NULL, ",", &tail);
        }
    }
  else
    {
      debug ("fopen() failed: %s ", strerror (errno));
      ret = false;
    }

  debug ("pid %d is%s under `%s' controllers", pid, (ret ? "" : " not"),
         controllers);
  return ret;
}


static void
fcgi_cgroups_list_controllers_by_mountpoint (FCGX_Request * request,
                                             const char *mountpoint)
{
  int rc;
  void *handle = NULL;
  struct cgroup_mount_point controller;
  int contr_count = 0;

  FCGX_PutS ("[", request->out);

  rc = cgroup_get_controller_begin (&handle, &controller);
  debug ("cgroup_get_controller_begin() returned %d", rc);

  while (0 == rc)
    {
      debug ("controller `%s', mount point `%s'", controller.name,
             controller.path);
      if (0 == strcmp (mountpoint, controller.path))
        {
          if (contr_count > 0)
            FCGX_PutS (", ", request->out);
          FCGX_FPrintF (request->out, "\"%s\"", controller.name);
          contr_count++;
        }
      rc = cgroup_get_controller_next (&handle, &controller);
    }
  debug ("exit from loop with %d", rc);

  cgroup_get_controller_end (&handle);

  FCGX_PutS ("]", request->out);
}


static void
fcgi_cgroups_list_groups (FCGX_Request * request, const char *controller,
                          const char *path, const char *mountpoint)
{
  int rc;
  void *handle = NULL;
  int base_level = 0;
  struct cgroup_file_info info;
  int group_count = 0;

  FCGX_PutS ("[", request->out);

  size_t mountpoint_len = strlen (mountpoint);

  rc =
    cgroup_walk_tree_begin (controller, path, 0, &handle, &info, &base_level);
  debug ("cgroup_walk_tree_begin() returned %d", rc);

  while (0 == rc)
    {
      if (CGROUP_FILE_TYPE_DIR == info.type)
        {
          if (group_count > 0)
            FCGX_PutS (", ", request->out);

          const char *rel_path = info.full_path + mountpoint_len;
          while ('/' == *rel_path)
            rel_path++;

          FCGX_FPrintF (request->out, "\"/%s\"", rel_path);

          debug ("full path = `%s', rel path = `%s'", info.full_path,
                 rel_path);

          group_count++;
        }
      rc = cgroup_walk_tree_next (0, &handle, &info, base_level);
    }
  debug ("exit from loop with %d", rc);

  cgroup_walk_tree_end (&handle);

  FCGX_PutS ("]", request->out);
}


static void
fcgi_cgroups_heirarchy (FCGX_Request * request, const char *controller,
                        const char *path, const char *mountpoint)
{
  FCGX_PutS ("{", request->out);

  FCGX_PutS ("controllers: ", request->out);
  fcgi_cgroups_list_controllers_by_mountpoint (request, mountpoint);

  FCGX_PutS (", ", request->out);

  FCGX_PutS ("groups: ", request->out);
  fcgi_cgroups_list_groups (request, controller, path, mountpoint);

  FCGX_PutS ("}", request->out);
}


static void
fcgi_cgroups_list_hierarhies (FCGX_Request * request, const char *controllers,
                              const char *path)
{
  int rc;
  void *handle = NULL;
  struct cgroup_mount_point controller;
  char mountpoint[FILENAME_MAX];        // it's a sizeof cgroup_mount_point.path
  int hier_count = 0;

  FCGX_PutS ("[", request->out);

  mountpoint[0] = '\0';

  debug ("controllers `%s', path `%s'", controllers, path);

  rc = cgroup_get_controller_begin (&handle, &controller);
  debug ("cgroup_get_controller_begin() returned %d", rc);

  while (0 == rc)
    {
      debug ("controller `%s', mount point `%s'", controller.name,
             controller.path);

      bool use_controller =
        controller_is_in_list (controllers, controller.name)
        && group_has_controller (controller.name, path);

      if (use_controller)
        {
          // XXX are controllers sorted by mount point?
          // XXX use stat() and st_dev/st_ino ?
          if (0 != strcmp (mountpoint, controller.path))        // new mount point (hierarchy)
            {
              strcpy (mountpoint, controller.path);
              if (hier_count > 0)
                FCGX_PutS (", ", request->out);
              fcgi_cgroups_heirarchy (request, controller.name, path,
                                      mountpoint);
              hier_count++;
            }
        }
      rc = cgroup_get_controller_next (&handle, &controller);
    }
  debug ("exit from loop with %d", rc);

  cgroup_get_controller_end (&handle);

  FCGX_PutS ("]", request->out);
}


static void
fcgi_cgroups_list_tasks (FCGX_Request * request, const char *controllers,
                         const char *path)
{
  int rc;
  pid_t pid;
  void *handle = NULL;
  int pid_count = 0;

  FCGX_PutS ("[", request->out);

  if (group_exists (controllers, path))
    {
      const char *first_controller;
      char *other_controllers;
      size_t l = strlen (controllers) + 1;
      char cntlrs[l];
      memcpy (cntlrs, controllers, l);
      first_controller = strtok_r (cntlrs, ",", &other_controllers);
      rc = cgroup_get_task_begin (path, first_controller, &handle, &pid);
      debug ("cgroup_get_task_begin() returned %d", rc);

      while (0 == rc)
        {
          if ((NULL == other_controllers)
              || group_has_pid (other_controllers, path, pid))
            {
              if (pid_count > 0)
                FCGX_PutS (", ", request->out);
              FCGX_FPrintF (request->out, "%d", pid);
              pid_count++;
            }
          rc = cgroup_get_task_next (&handle, &pid);
        }
      debug ("exit from loop with %d", rc);

      cgroup_get_task_end (&handle);
    }
  else
    {
      debug ("group `%s:%s' does not exist", controllers, path);
    }

  FCGX_PutS ("]", request->out);
}


static void
fcgi_cgroups_attach_task (FCGX_Request * request, const char *controllers,
                          const char *path, const char *pid_s)
{
  char *p;
  pid_t pid = strtoul (pid_s, &p, 10);

  if ((pid <= 0) || ('\0' != *p))
    {
      debug ("invalid pid: %s", pid_s);
      FCGX_FPrintF (request->out, "{error: \"Invalid pid\"}");
      return;
    }

  size_t controllers_len = strlen (controllers) + 1;
  char controllers_copy[controllers_len];
  memcpy (controllers_copy, controllers, controllers_len);

  size_t number_of_controllers = count_controllers (controllers);
  const char *controllers_list[number_of_controllers + 1];      // + sentinel
  memset (controllers_list, 0, sizeof (controllers_list));

  char *tail = NULL;
  int controller_index = 0;
  char *next_controller = strtok_r (controllers_copy, ",", &tail);
  do
    {
      debug ("controller[%d] = %s", controller_index, next_controller);
      controllers_list[controller_index] = next_controller;
      next_controller = strtok_r (NULL, ",", &tail);
      controller_index++;
    }
  while (NULL != next_controller);

  int rc = cgroup_change_cgroup_path (path, pid, controllers_list);
  if (0 != rc)
    {
      debug ("cgroup_change_cgroup_path() returned %d (%s)", rc,
             cgroup_strerror (rc));
      FCGX_FPrintF (request->out, "{error: \"%s\"}", cgroup_strerror (rc));
    }
  else
    FCGX_FPrintF (request->out, "{}");
}


static void
fcgi_cgroups_action (FCGX_Request * request, const char *controllers,
                     const char *path, const char *action)
{
  size_t l = strlen (action) + 1;

  char act[l];

  memcpy (act, action, l);

  char *arg = strchr (act, '=');
  if (NULL != arg)
    {
      *arg = '\0';
      arg++;
    }

  debug ("action `%s', argument `%s'", act, arg);

  if (0 == strcmp ("list", act))
    fcgi_cgroups_list_hierarhies (request, controllers, path);
  else if (0 == strcmp ("list-tasks", act))
    fcgi_cgroups_list_tasks (request, controllers, path);
  else if (0 == strcmp ("attach-task", act))
    fcgi_cgroups_attach_task (request, controllers, path, arg);
}


void
fcgi_cgroups (FCGX_Request * request, char **uri_tail)
{
  // controllers:path?action
  char *controllers;
  char *path = NULL;
  char *action = NULL;

  while ('/' == **uri_tail)
    (*uri_tail)++;

  char *colon = strchr (*uri_tail, ':');
  if (NULL != colon)
    {
      *colon = '\0';
      path = colon + 1;
      controllers = *uri_tail;
    }
  else
    {
      controllers = "*";
      path = *uri_tail;
    }

  char *ques = strchr (path, '?');
  if (NULL != ques)
    {
      *ques = '\0';
      action = ques + 1;
    }

  trim_trailing_slashes (path);

  debug ("controllers `%s', path `%s', action `%s'", controllers, path,
         action);

  if ((NULL == action) || ('\0' == action[0]))
    fcgi_cgroups_list_hierarhies (request, controllers, path);
  else
    fcgi_cgroups_action (request, controllers, path, action);
}
