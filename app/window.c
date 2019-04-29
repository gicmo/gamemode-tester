/* window.c
 *
 * Copyright 2019 Christian Kellner
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "window.h"
#include "gamemode_client.h"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>


struct _GmtWindow
{
  GtkApplicationWindow parent_instance;

  /* Template widgets */
  GtkHeaderBar *header_bar;
  GtkLabel     *lbl_pid;
  GtkSwitch    *sw_gamemode;
  GtkSwitch    *sw_uselib;

  /* config */
  gboolean uselib;
  gboolean portal;

  /*  */
  int pid;

  /*  */
  GDBusProxy *gamemode;
};

G_DEFINE_TYPE (GmtWindow, gmt_window, GTK_TYPE_APPLICATION_WINDOW)

#define GET_PRIV(self) G_STRUCT_MEMBER_P (self, BoltExported_private_offset)

/* prototypes */
static void     on_bus_ready (GObject      *source,
                              GAsyncResult *res,
                              gpointer      user_data);

static void     gmt_builtin_gamemode_switch (GmtWindow *self,
                                             gboolean   enable);

static void     gmt_portal_gamemode_switch (GmtWindow *self,
                                            gboolean   enable);

static void     on_builtin_switch_ready (GObject      *source,
                                         GAsyncResult *res,
                                         gpointer      user_data);

static void     gmt_library_gamemode_switch (GmtWindow *self,
                                             gboolean   enable);

/* ui signals */
static gboolean on_gamemode_toggled (GmtWindow *self,
                                     gboolean   enable,
                                     GtkSwitch *toggle);

static void     on_uselib_notify (GObject    *gobject,
                                  GParamSpec *pspec,
                                  gpointer    user_data);

int gmt_setup_socket (GmtWindow          *self,
                      struct sockaddr_un *sau,
                      socklen_t          *sl);
/* private stuff */

static void
gmt_window_class_init (GmtWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/GameModeTester/window.ui");
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, header_bar);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, lbl_pid);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, sw_uselib);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, sw_gamemode);

  gtk_widget_class_bind_template_callback (widget_class, on_gamemode_toggled);
}

#define GAMEMODE_DBUS_NAME "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_IFACE "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_PATH "/com/feralinteractive/GameMode"

#define PORTAL_DBUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_DBUS_IFACE "org.freedesktop.portal.GameMode"
#define PORTAL_DBUS_PATH "/org/freedesktop/portal/desktop"

static void
gmt_window_init (GmtWindow *self)
{
  g_autoptr(GCredentials) creds = NULL;
  g_autofree char *pidstr = NULL;
  GDBusProxyFlags flags;
  gboolean portal = TRUE;

  self->portal = TRUE;

  gtk_widget_init_template (GTK_WIDGET (self));

  creds = g_credentials_new ();
  self->pid = g_credentials_get_unix_pid (creds, NULL);
  pidstr = g_strdup_printf ("%d", self->pid);
  gtk_label_set_text (self->lbl_pid, pidstr);

  flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION;
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            flags, NULL,
                            portal ? PORTAL_DBUS_NAME : GAMEMODE_DBUS_NAME,
                            portal ? PORTAL_DBUS_PATH : GAMEMODE_DBUS_PATH,
                            portal ? PORTAL_DBUS_IFACE : GAMEMODE_DBUS_IFACE,
                            NULL,
                            on_bus_ready,
                            self);

  self->uselib = TRUE;
  g_signal_connect_object (self->sw_uselib, "notify::active",
                           G_CALLBACK (on_uselib_notify),
                           self, 0);
}

static void
on_bus_ready (GObject      *source,
              GAsyncResult *res,
              gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  GmtWindow *self;
  const char *name;

  self = GMT_WINDOW (user_data);

  self->gamemode = g_dbus_proxy_new_for_bus_finish (res, &err);

  if (self->gamemode == NULL)
    {
      g_warning ("could not create gamemode proxy: %s", err->message);
      return;
    }
  name = g_dbus_connection_get_unique_name (g_dbus_proxy_get_connection (self->gamemode));

  g_debug ("my name: %s", name);

  gtk_widget_set_sensitive (GTK_WIDGET (self->sw_gamemode), TRUE);
}

static gboolean
on_gamemode_toggled (GmtWindow *self,
                     gboolean   enable,
                     GtkSwitch *toggle)
{
  g_debug ("Toggled: %s", (enable ? "enable" : "disable"));

  gtk_widget_set_sensitive (GTK_WIDGET (self->sw_gamemode), FALSE);

  g_debug ("use gamemode library: %s", (self->uselib ? "yes" :  "no"));

  if (self->uselib)
    gmt_library_gamemode_switch (self, enable);
  else if (self->portal)
    gmt_portal_gamemode_switch (self, enable);
  else
    gmt_builtin_gamemode_switch (self, enable);

  return TRUE;
}

static void
gamemode_toggle_finish (GmtWindow *self, int r)
{
  gboolean active;
  gboolean state;

  g_debug ("toggle finish: %d", r);

  active = gtk_switch_get_active (self->sw_gamemode);

  if (active)
    {
      /*
       *  0 if the request was accepted and the client could be registered
       * -1 if the request was accepted but the client could not be registered
       * -2 if the request was rejected
       * */

      state = r == 0;
    }
  else
    {
      /*
       *  0 if the request was accepted and the client existed
       * -1 if the request was accepted but the client did not exist
       * -2 if the request was rejected
       */
      state = r != -2;
    }

  g_signal_handlers_block_by_func (self->sw_gamemode, on_gamemode_toggled, self);

  if (r == 0)
    gtk_switch_set_state (self->sw_gamemode, active);
  else
    gtk_switch_set_active (self->sw_gamemode, state);

  g_signal_handlers_unblock_by_func (self->sw_gamemode, on_gamemode_toggled, self);

  gtk_widget_set_sensitive (GTK_WIDGET (self->sw_gamemode), TRUE);
}

static void
on_uselib_notify (GObject    *gobject,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
  GmtWindow *self = GMT_WINDOW (user_data);

  self->uselib = gtk_switch_get_active (self->sw_uselib);
}

/* native gamemode implementation */
typedef struct PortalCall
{
  GmtWindow *wnd;
  int        wire[2];
  int        data[2];
} PortalCall;

static gboolean
make_socketpair (int domain, int type, int proto, int sv[2], GError **error)
{
  int r;

  r = socketpair (domain, type, proto, sv);

  if (r == -1)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "could not create socket pair: %s",
                   g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

static void
portal_call_destroy (PortalCall *call)
{
  if (call == NULL)
    return;

  close (call->wire[0]);
  close (call->wire[1]);

  close (call->data[0]);
  close (call->data[1]);

  g_object_unref (call->wnd);
  g_slice_free (PortalCall, call);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PortalCall, portal_call_destroy);

PortalCall *
gmt_window_portal_call_new (GmtWindow *self, GError **error)
{
  g_autoptr(PortalCall) call = NULL;
  gboolean ok;

  call = g_slice_new0 (PortalCall);

  ok = make_socketpair (AF_UNIX, SOCK_STREAM, 0, call->wire, error);

  if (!ok)
    return NULL;

  ok = make_socketpair (AF_UNIX, SOCK_STREAM, 0, call->data, error);

  if (!ok)
    return NULL;

  call->wnd = g_object_ref (self);

  return g_steal_pointer (&call);
}

static int
send_fd (int sock, const char *data, size_t len, int fd)
{
  struct iovec iov = {
    .iov_base = (void *) data,
    .iov_len = len,
  };

  union
  {
    struct cmsghdr cmh;
    char           data[CMSG_SPACE (sizeof (int))];
  } ctrl = {};
  struct msghdr msg = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
  };
  int r;

  if (fd > 0)
    {
      struct cmsghdr *cmh;

      msg.msg_control = &ctrl;
      msg.msg_controllen = sizeof (ctrl);

      cmh = CMSG_FIRSTHDR (&msg);

      cmh->cmsg_level = SOL_SOCKET;
      cmh->cmsg_type = SCM_RIGHTS;
      cmh->cmsg_len = CMSG_LEN (sizeof (int));

      memcpy (CMSG_DATA (cmh), &fd, sizeof (int));

    }

  r = sendmsg (sock, &msg, MSG_NOSIGNAL);

  if (r < 0)
    return -errno;

  return 0;
}


static void
on_portal_switch_ready (GObject      *source,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) val = NULL;
  g_autoptr(PortalCall) call = user_data;
  GmtWindow *self = call->wnd;
  int r = -1;

  val =  g_dbus_proxy_call_with_unix_fd_list_finish (G_DBUS_PROXY (self->gamemode),
                                                     NULL,
                                                     res,
                                                     &err);
  if (val == NULL)
    {
      g_warning ("could not talk to gamemode: %s", err->message);
    }
  else
    {
      g_variant_get (val, "(i)", &r);

      if (r != 0)
        g_warning ("request got rejected: %d", r);
    }

  gamemode_toggle_finish (self, r);
}


static void
gmt_portal_gamemode_switch (GmtWindow *self,
                            gboolean   enable)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GUnixFDList) fds = NULL;
  g_autofree char *path = NULL;
  const char *mode = enable ? "RegisterGame" : "UnregisterGame";
  PortalCall *call;
  ssize_t r;

  call = gmt_window_portal_call_new (self, &err);

  if (call == NULL)
    {
      g_warning ("could not create call: %s", err->message);
      gamemode_toggle_finish (self, -1);
      return;
    }

  fds = g_unix_fd_list_new_from_array (&call->wire[1], 1);

  g_dbus_proxy_call_with_unix_fd_list (G_DBUS_PROXY (self->gamemode),
                                       "Action",
                                       g_variant_new ("(h)", 0),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       fds,
                                       NULL, /* cancel */
                                       on_portal_switch_ready,
                                       call);

  g_debug ("sending fd");
  r = send_fd (call->wire[0], mode, strlen (mode), call->data[1]);
  if (r < 0)
    g_warning ("could not send fd: %s", g_strerror (errno));
  else
    g_debug ("fd sent");
}


static void
gmt_builtin_gamemode_switch (GmtWindow *self,
                             gboolean   enable)
{
  GVariant *params;
  const char *mode = enable ? "RegisterGame" : "UnregisterGame";

  g_debug ("Toggled: %s", (enable ? "enable" : "disable"));

  params = g_variant_new ("(i)", self->pid);

  g_dbus_proxy_call (G_DBUS_PROXY (self->gamemode),
                     mode,
                     params,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL, /* cancel */
                     on_builtin_switch_ready,
                     self);
}

static void
on_builtin_switch_ready (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) val = NULL;
  GmtWindow *self = user_data;
  int r = -1;

  val = g_dbus_proxy_call_finish (G_DBUS_PROXY (self->gamemode), res, &err);
  if (val == NULL)
    {
      g_warning ("could not talk to gamemode: %s", err->message);
    }
  else
    {
      g_variant_get (val, "(i)", &r);

      if (r != 0)
        g_warning ("request got rejected: %d", r);
    }

  gamemode_toggle_finish (self, r);
}

/* gamemode library */
static void
switch_gamemode_thread (GTask        *task,
                        gpointer      source_object,
                        gpointer      task_data,
                        GCancellable *cancellable)
{
  gboolean enable = !!GPOINTER_TO_INT (task_data);
  int r;

  if (enable)
    r = gamemode_request_start ();
  else
    r = gamemode_request_end ();

  g_task_return_int (task, r);
}


static void
on_library_switch_ready (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (res);
  GmtWindow *wnd = GMT_WINDOW (source);
  int r;

  r = (int) g_task_propagate_int (task, NULL);

  gamemode_toggle_finish (wnd, r);
}

static void
gmt_library_gamemode_switch (GmtWindow *self,
                             gboolean   enable)
{
  GTask *task;

  task = g_task_new (self, NULL, on_library_switch_ready, self);
  g_task_set_task_data (task, GINT_TO_POINTER ((gint) enable), NULL);
  g_task_run_in_thread (task, switch_gamemode_thread);
}
