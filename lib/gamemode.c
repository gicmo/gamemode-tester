
#include <config.h>

#include <stdio.h>
#include <sys/types.h>
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
#define DEBUG(...) fprintf (stderr, "GAMEMODE: " # __VA_ARGS__)
#define LOG_ERROR DEBUG ("ERROR:" # error_string)
#endif

#ifdef DO_TRACE
#define TRACE(...) fprintf (stderr, "GAMEMODE-TRACE: " #  __VA_ARGS__)
#else
#define TRACE(...)
#endif

#define _cleanup_(x) __attribute__((cleanup (x)))
#define _cleanup_bus_ _cleanup_ (hop_off_the_bus)
#define _cleanup_msg_ _cleanup_ (cleanup_msg)
#define _cleanup_dpc_ _cleanup_ (cleanup_pending_call)

/* globals */
static char error_string[512] = { 0 };

/* utils */
static void
hop_off_the_bus (DBusConnection **bus)
{
  if (bus == NULL)
    return;

  dbus_connection_unref (*bus);
  TRACE ("Im off the bus!");
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
      snprintf (error_string, sizeof (error_string),
                "Could not connect to bus: %s", err.message);

      LOG_ERROR;
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

  bus = hop_on_the_bus ();
  pid = (dbus_int32_t) getpid ();

  msg = dbus_message_new_method_call (GAMEMODE_DBUS_NAME,
                                      GAMEMODE_DBUS_PATH,
                                      GAMEMODE_DBUS_IFACE,
                                      method);
  if (!msg)
    {
      snprintf (error_string, sizeof (error_string),
                "Could not create dbus message");
      LOG_ERROR;
      return -1;
    }

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
    {
      snprintf (error_string, sizeof (error_string),
                "Did not receive a reply");
      LOG_ERROR;
      return -1;
    }

  dbus_error_init (&err);

  if (dbus_set_error_from_message (&err, msg))
    {
      snprintf (error_string, sizeof (error_string),
                "Could not call method '%s' on '%s': %s",
                method, GAMEMODE_DBUS_IFACE, err.message);
      LOG_ERROR;
    }
  else if (!dbus_message_iter_init (msg, &iter) ||
           dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INT32)
    {
      snprintf (error_string, sizeof (error_string),
                "Could not unmarshal dbus message");
      LOG_ERROR;
    }
  else
    {
      dbus_message_iter_get_basic (&iter, &res);
    }

  return res;
}

/* the external API */

extern const char *
real_gamemode_error_string (void)
{
  return error_string;
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
