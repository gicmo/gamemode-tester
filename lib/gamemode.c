/* Copyright (c) 2019, Christian Kellner All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 *
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of Feral Interactive nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * RISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <config.h>

#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dbus/dbus.h>

/* macros */
#define GAMEMODE_DBUS_NAME "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_IFACE "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_PATH "/com/feralinteractive/GameMode"

#define DO_TRACE 1

#ifdef NDEBUG
#define DEBUG(...)
#define LOG_ERROR
#else
#define DEBUG(...) fprintf (stderr, __VA_ARGS__)
#define LOG_ERROR fprintf (stderr, "ERROR: %s \n", error_string)
#endif

#ifdef DO_TRACE
#define TRACE(...) fprintf (stderr, __VA_ARGS__)
#else
#define TRACE(...)
#endif

#define _cleanup_(x) __attribute__((cleanup (x)))
#define _cleanup_bus_ _cleanup_ (hop_off_the_bus)
#define _cleanup_msg_ _cleanup_ (cleanup_msg)
#define _cleanup_dpc_ _cleanup_ (cleanup_pending_call)

/* globals */
static char error_log[512] = { 0 };

/* utils */
static int log_error (const char *fmt,
                      ...) __attribute__((format (printf, 1, 2)));

static int
in_flatpak (void)
{
  struct stat sb;
  int r;

  r = lstat ("/.flatpak-info", &sb);

  return r == 0 && sb.st_size > 0;
}

static int
log_error (const char *fmt, ...)
{
  va_list args;
  int n;

  va_start (args, fmt);
  n = vsnprintf (error_log, sizeof (error_log), fmt, args);
  va_end (args);

  if (n > sizeof (error_log))
    DEBUG ("Error log overflow");

#ifndef NDEBUG
  fprintf (stderr, "ERROR: %s \n", error_log);
#endif

  return -1;
}

static void
hop_off_the_bus (DBusConnection **bus)
{
  if (bus == NULL)
    return;

  dbus_connection_unref (*bus);
}

static DBusConnection *
hop_on_the_bus (void)
{
  DBusConnection *bus;
  DBusError err;

  dbus_error_init (&err);

  bus = dbus_bus_get (DBUS_BUS_SESSION, &err);

  if (bus == NULL)
    {
      log_error ("Could not connect to bus: %s", err.message);
      dbus_error_free (&err);
    }

  return bus;
}

static void
cleanup_msg (DBusMessage **msg)
{
  if (msg == NULL)
    return;

  dbus_message_unref (*msg);
}

static void
cleanup_pending_call (DBusPendingCall **call)
{
  if (call == NULL)
    return;

  dbus_pending_call_unref (*call);
}

static int
gamemode_request (const char *method, pid_t for_pid)
{
  _cleanup_bus_ DBusConnection *bus = NULL;
  _cleanup_msg_ DBusMessage *msg = NULL;
  _cleanup_dpc_ DBusPendingCall *call = NULL;
  DBusMessageIter iter;
  DBusError err;
  dbus_int32_t pid;
  int res = -1;
  int fp;

  bus = hop_on_the_bus ();
  pid = (dbus_int32_t) getpid ();

  fp = in_flatpak ();
  TRACE ("[%d] request '%s' received (%d) [flatpak: %s]\n",
         (int) pid, method, (int) for_pid, (fp ? "y" : "n"));

  msg = dbus_message_new_method_call (GAMEMODE_DBUS_NAME,
                                      GAMEMODE_DBUS_PATH,
                                      GAMEMODE_DBUS_IFACE,
                                      method);
  if (!msg)
    return log_error ("Could not create dbus message");


  dbus_message_iter_init_append (msg, &iter);
  dbus_message_iter_append_basic (&iter, DBUS_TYPE_INT32, &pid);

  if (for_pid != 0)
    {
      dbus_int32_t p = (dbus_int32_t) for_pid;
      dbus_message_iter_append_basic (&iter, DBUS_TYPE_INT32, &p);
    }

  dbus_connection_send_with_reply (bus, msg, &call, -1);
  dbus_connection_flush (bus);
  dbus_message_unref (msg);
  msg = NULL;

  dbus_pending_call_block (call);
  msg = dbus_pending_call_steal_reply (call);

  if (msg == NULL)
    return log_error ("Did not receive a reply");

  dbus_error_init (&err);

  if (dbus_set_error_from_message (&err, msg))
    log_error ("Could not call method '%s' on '%s': %s",
               method, GAMEMODE_DBUS_IFACE, err.message);
  else if (!dbus_message_iter_init (msg, &iter) ||
           dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INT32)
    log_error ("Could not unmarshal dbus message");
  else
    dbus_message_iter_get_basic (&iter, &res);

  TRACE ("[%d] request '%s' done: %d\n",
         (int) pid, method, res);

  return res;
}

/* the external API */

extern const char *
real_gamemode_error_string (void)
{
  return error_log;
}

extern int
real_gamemode_request_start (void)
{
  return gamemode_request ("RegisterGame", 0);
}

extern int
real_gamemode_request_end (void)
{
  return gamemode_request ("UnregisterGame", 0);
}

extern int
real_gamemode_query_status (void)
{
  return gamemode_request ("QueryStatus", 0);
}

extern int
real_gamemode_request_start_for (pid_t pid)
{
  return gamemode_request ("RegisterGameByPID", pid);
}

extern int
real_gamemode_request_end_for (pid_t pid)
{
  return gamemode_request ("UnregisterGameByPID", pid);
}

extern int
real_gamemode_query_status_for (pid_t pid)
{
  return gamemode_request ("QueryStatusByPID", pid);
}
