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

#include <math.h>
#include <limits.h>

struct _GmtWindow
{
  GtkApplicationWindow parent_instance;

  /* Template widgets */
  GtkHeaderBar *header_bar;
  GtkLabel     *lbl_pid;
  GtkSwitch    *sw_gamemode;
  GtkSwitch    *sw_uselib;
  GtkLabel     *lbl_flatpak;
  GtkLabel     *lbl_status;
  GtkButton    *btn_refresh;

  GtkComboBox  *cbx_call;
  GtkEntry     *txt_target;
  GtkEntry     *txt_requester;
  GtkButton    *btn_call;
  GtkLabel     *lbl_result;


  /* config */
  gboolean uselib;
  gboolean portal;

  /*  */
  int pid;

  /*  */
  GDBusProxy *gamemode;

  /*  */
  GtkSwitch    *sw_work;
  GCancellable *work_cancel;
};

G_DEFINE_TYPE (GmtWindow, gmt_window, GTK_TYPE_APPLICATION_WINDOW)

#define GET_PRIV(self) G_STRUCT_MEMBER_P (self, BoltExported_private_offset)

/* prototypes */
static void     on_bus_ready (GObject      *source,
                              GAsyncResult *res,
                              gpointer      user_data);

static void     gmt_builtin_gamemode_switch (GmtWindow *self,
                                             gboolean   enable);

static void     gmt_builtin_query_status (GmtWindow *self);

static void     on_builtin_switch_ready (GObject      *source,
                                         GAsyncResult *res,
                                         gpointer      user_data);

static void     gmt_library_gamemode_switch (GmtWindow *self,
                                             gboolean   enable);

static void     gmt_library_query_status (GmtWindow *self);

/* ui signals */
static gboolean on_gamemode_toggled (GmtWindow *self,
                                     gboolean   enable,
                                     GtkSwitch *toggle);

static void     on_refresh_clicked (GmtWindow *self,
                                    GtkButton *button);

static void     on_uselib_notify (GObject    *gobject,
                                  GParamSpec *pspec,
                                  gpointer    user_data);

static void     on_docall_clicked (GmtWindow *self,
                                   GtkButton *button);

static void     on_call_selected  (GmtWindow *self,
                                   GtkComboBox *widget);

static gboolean on_work_toggled (GmtWindow *self,
                                 gboolean   enable,
                                 GtkSwitch *toggle);

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
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, lbl_flatpak);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, lbl_status);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, btn_refresh);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, sw_work);

  gtk_widget_class_bind_template_child (widget_class, GmtWindow, cbx_call);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, txt_target);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, txt_requester);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, btn_call);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, lbl_result);

  gtk_widget_class_bind_template_callback (widget_class, on_gamemode_toggled);
  gtk_widget_class_bind_template_callback (widget_class, on_refresh_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_call_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_docall_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_work_toggled);
}

#define GAMEMODE_DBUS_NAME "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_IFACE "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_PATH "/com/feralinteractive/GameMode"

#define PORTAL_DBUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_DBUS_IFACE "org.freedesktop.portal.GameMode"
#define PORTAL_DBUS_PATH "/org/freedesktop/portal/desktop"

static int
in_flatpak (void)
{
  struct stat sb;
  int r;

  r = lstat ("/.flatpak-info", &sb);

  return r == 0 && sb.st_size > 0;
}

static void
gmt_window_init (GmtWindow *self)
{
  g_autoptr(GCredentials) creds = NULL;
  g_autofree char *pidstr = NULL;
  gboolean boxed;

  boxed = in_flatpak ();

  self->portal = boxed;

  gtk_widget_init_template (GTK_WIDGET (self));

  creds = g_credentials_new ();
  self->pid = g_credentials_get_unix_pid (creds, NULL);

  pidstr = g_strdup_printf ("%d", self->pid);
  gtk_label_set_text (self->lbl_pid, pidstr);

  self->uselib = TRUE;
  g_signal_connect_object (self->sw_uselib, "notify::active",
                           G_CALLBACK (on_uselib_notify),
                           self, 0);


  gtk_label_set_text (self->lbl_flatpak, boxed ? "yes" : "no");

  on_call_selected (self, self->cbx_call);

  gtk_entry_set_text (self->txt_target, pidstr);
  gtk_entry_set_text (self->txt_requester, pidstr);

  self->work_cancel = g_cancellable_new ();
}

static void
gmt_startstop_operation (GmtWindow *self, gboolean starting)
{
  gtk_widget_set_sensitive (GTK_WIDGET (self->sw_gamemode), !starting);
  gtk_widget_set_sensitive (GTK_WIDGET (self->sw_uselib), !starting);
  gtk_widget_set_sensitive (GTK_WIDGET (self->btn_refresh), !starting);

  gtk_widget_set_sensitive (GTK_WIDGET (self->cbx_call), !starting);
  gtk_widget_set_sensitive (GTK_WIDGET (self->txt_target), !starting);
  gtk_widget_set_sensitive (GTK_WIDGET (self->txt_requester), !starting);
  gtk_widget_set_sensitive (GTK_WIDGET (self->btn_call), !starting);

}

static gboolean
on_gamemode_toggled (GmtWindow *self,
                     gboolean   enable,
                     GtkSwitch *toggle)
{
  g_debug ("Toggled: %s", (enable ? "enable" : "disable"));

  gmt_startstop_operation (self, TRUE);

  g_debug ("use gamemode library: %s", (self->uselib ? "yes" :  "no"));

  if (self->uselib)
    gmt_library_gamemode_switch (self, enable);
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

  gmt_startstop_operation (self, FALSE);
}

static void
on_uselib_notify (GObject    *gobject,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
  GmtWindow *self = GMT_WINDOW (user_data);

  self->uselib = gtk_switch_get_active (self->sw_uselib);
}

/* refresh handling */
static void
on_refresh_clicked (GmtWindow *self,
                    GtkButton *button)
{
  g_debug ("Refreshing");

  gmt_startstop_operation (self, TRUE);

  g_debug ("use gamemode library: %s", (self->uselib ? "yes" :  "no"));

  if (self->uselib)
    gmt_library_query_status (self);
  else
    gmt_builtin_query_status (self);
}

static void
refresh_finish (GmtWindow *self, int r)
{
  g_autofree char *text = NULL;

  text = g_strdup_printf ("%d", r);
  gtk_label_set_text (self->lbl_status, text);

  gmt_startstop_operation (self, FALSE);
}

static const char *
call_get_id (GmtWindow *self, gint *args)
{
  g_auto(GValue) val = G_VALUE_INIT;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gboolean ok;

  model = gtk_combo_box_get_model (self->cbx_call);
  ok = gtk_combo_box_get_active_iter (self->cbx_call, &iter);

  if (!ok)
    return NULL;

  gtk_tree_model_get_value (model, &iter, 2, &val);

  if (args)
    *args = g_value_get_int (&val);

  return gtk_combo_box_get_active_id (self->cbx_call);
}

static void
on_call_selected (GmtWindow *self,
                  GtkComboBox *box)
{
  const char *method;
  gint args;

  method = call_get_id (self, &args);

  if (method == NULL)
    return;

  gtk_widget_set_sensitive (GTK_WIDGET (self->txt_requester), args > 1);
  gtk_widget_set_visible (GTK_WIDGET (self->txt_requester), args > 1);
}

/* native gamemode implementation */
typedef struct CallData_
{
  char       *method;
  GVariant   *params;
  gboolean    portal;
  GDBusProxy *proxy;
} CallData;

static void
call_data_free (gpointer data)
{
  CallData *call = data;

  g_free (call->method);
  g_variant_unref (call->params);
  g_clear_object (&call->proxy);
  g_slice_free (CallData, call);
}

static void
on_gamemode_call_ready (GObject      *source,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) val = NULL;
  GTask *task = G_TASK (user_data);
  CallData *call;
  int r = -1;

  call = g_task_get_task_data (task);

  val = g_dbus_proxy_call_finish (G_DBUS_PROXY (call->proxy), res, &err);
  if (val == NULL)
    {
      g_warning ("could not talk to gamemode: %s", err->message);
      g_task_return_error (task, g_steal_pointer (&err));
      return;
    }

  g_variant_get (val, "(i)", &r);
  if (r < 0)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "GameMode error"); //TODO: better error message
      return;
    }

  g_task_return_int (task, r);
  g_object_unref (task);
}

static void
on_bus_ready (GObject      *source,
              GAsyncResult *res,
              gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  GTask *task = user_data;
  CallData *call;
  const char *name;

  call = g_task_get_task_data (task);
  call->proxy = g_dbus_proxy_new_for_bus_finish (res, &err);

  if (call->proxy == NULL)
    {
      g_warning ("could not create gamemode proxy: %s", err->message);
      g_task_return_error (task, g_steal_pointer (&err));
      return;
    }
  name = g_dbus_connection_get_unique_name (g_dbus_proxy_get_connection (call->proxy));

  g_debug ("my name: %s", name);

  g_dbus_proxy_call (G_DBUS_PROXY (call->proxy),
                     call->method,
                     call->params,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL, /* cancel */
                     on_gamemode_call_ready,
                     task);
}

static void
call_gamemode (GmtWindow          *window,
               const char         *method,
               GVariant           *params,
               gboolean            portal,
               GAsyncReadyCallback callback,
               gpointer            user_data)
{
  CallData *data;
  GTask *task;
  GDBusProxyFlags flags;

  data = g_slice_new0 (CallData);
  data->method = g_strdup (method);
  data->params = g_variant_ref_sink (params);
  data->portal = portal;

  task = g_task_new (window, NULL, callback, user_data);
  g_task_set_task_data (task, data, call_data_free);

  flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION;
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            flags, NULL,
                            portal ? PORTAL_DBUS_NAME : GAMEMODE_DBUS_NAME,
                            portal ? PORTAL_DBUS_PATH : GAMEMODE_DBUS_PATH,
                            portal ? PORTAL_DBUS_IFACE : GAMEMODE_DBUS_IFACE,
                            NULL,
                            on_bus_ready,
                            task);
}

static int
call_gamemode_finish (GAsyncResult *res,
                      GError      **error)
{
  gssize r;

  r = g_task_propagate_int (G_TASK (res), error);

  return (int) r;
}

static void
on_builtin_switch_ready (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  GmtWindow *self = user_data;
  int r = -1;

  r = call_gamemode_finish (res, &err);
  if (r < 0)
    g_warning ("could not talk to gamemode: %s", err->message);

  gamemode_toggle_finish (self, r);
}

static void
gmt_builtin_gamemode_switch (GmtWindow *self,
                             gboolean   enable)
{
  GVariant *params;
  const char *mode = enable ? "RegisterGame" : "UnregisterGame";

  g_debug ("Toggled: %s", (enable ? "enable" : "disable"));

  params = g_variant_new ("(i)", self->pid);

  call_gamemode (self,
                 mode,
                 params,
                 self->portal,
                 on_builtin_switch_ready,
                 self);
}

static void
on_builtin_query_status_ready (GObject      *source,
                               GAsyncResult *res,
                               gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  GmtWindow *self = user_data;
  int r = -1;

  r = call_gamemode_finish (res, &err);
  if (r < 0)
    g_warning ("could not talk to gamemode: %s", err->message);

  refresh_finish (self, r);
}

static void
gmt_builtin_query_status (GmtWindow *self)
{
  GVariant *params;

  g_debug ("Builtin Query Status");

  params = g_variant_new ("(i)", self->pid);

  call_gamemode (self,
                 "QueryStatus",
                 params,
                 self->portal,
                 on_builtin_query_status_ready,
                 self);

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

static void
query_status_thread (GTask        *task,
                     gpointer      source_object,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
  int r;

  r = gamemode_query_status ();

  g_task_return_int (task, r);
}

static void
on_status_thread_ready (GObject      *source,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (res);
  GmtWindow *wnd = GMT_WINDOW (source);
  int r;

  r = (int) g_task_propagate_int (task, NULL);

  refresh_finish (wnd, r);
}

static void
gmt_library_query_status (GmtWindow *self)
{
  GTask *task;

  task = g_task_new (self, NULL, on_status_thread_ready, self);
  g_task_run_in_thread (task, query_status_thread);
}

/* custom calls */
static void
on_docall_ready (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *txt = NULL;
  GmtWindow *self = user_data;
  int r = -1;

  r = call_gamemode_finish (res, &err);
  if (r < 0)
    g_warning ("could not talk to gamemode: %s", err->message);

  txt = g_strdup_printf ("%d", r);

  gtk_label_set_text (self->lbl_result, txt);
  gmt_startstop_operation (self, FALSE);
}


static void
on_docall_clicked (GmtWindow *self,
                   GtkButton *button)
{
  g_autoptr(GError) err = NULL;
  GVariant *params;
  const char *method;
  const char *txt;
  gboolean ok;
  gint64 target = 0;
  gint64 requester = 0;
  gint args;

  method = call_get_id (self, &args);

  if (method == NULL)
    return;

  txt = gtk_entry_get_text (self->txt_target);
  ok = g_ascii_string_to_signed (txt, 10, 1, G_MAXINT32, &target, &err);

  if (!ok)
    {
      g_warning ("Failed to parse target pid: %s", err->message);
      return;
      }

  if (args > 1)
    {
      txt = gtk_entry_get_text (self->txt_requester);
      ok = g_ascii_string_to_signed (txt, 10, 1, G_MAXINT32, &requester, &err);

      if (!ok)
        {
          g_warning ("Failed to parse requestor pid: %s", err->message);
          return;
        }

      params = g_variant_new ("(ii)", (gint32) target, (gint32) requester);
    }
  else
    {
      params = g_variant_new ("(i)", (gint32) target);
    }

  g_debug ("do call: %s %i %i", method, (gint32) target, (gint32) requester);
  gmt_startstop_operation (self, TRUE);

  call_gamemode (self,
                 method,
                 params,
                 self->portal,
                 on_docall_ready,
                 self);

}

/*  */


static gboolean is_prime(long long unsigned int num, GCancellable *c) {
  /* no sqrt(num) optimization, because we
   * want the extra work */
  for (int i = 2; i < num && !g_cancellable_is_cancelled (c); i++)
    if (num % i == 0)
      return FALSE;

  return TRUE;
}

static void
calc_primes (GTask *task,
             gpointer source_object,
             gpointer task_data,
             GCancellable *cancellable)
{
  while (!g_cancellable_is_cancelled (cancellable))
    {

      for (long long unsigned int i = 2; i < ULLONG_MAX; i++)
        {
          gboolean isp;

          isp = is_prime (i, cancellable);
          if (isp)
            g_print("Found %llu to be prime\n", i);

          if (g_cancellable_is_cancelled (cancellable))
            break;
          }
      }

  g_task_return_boolean (task, TRUE);
}

static void
work_stopped (GObject *source_object,
              GAsyncResult *res,
              gpointer user_data)
{
  GTask *task = G_TASK (res);
  GmtWindow *self = g_task_get_source_object (task);

  gtk_widget_set_sensitive (GTK_WIDGET (self->sw_work), TRUE);
  g_cancellable_reset (self->work_cancel);
  gtk_switch_set_state (self->sw_work, FALSE);
}

static gboolean
on_work_toggled (GmtWindow *self,
                 gboolean   enable,
                 GtkSwitch *toggle)
{
  if (enable)
    {
      g_autoptr(GTask) task = NULL;

      task = g_task_new (self, self->work_cancel, work_stopped, NULL);
      g_task_run_in_thread (task, calc_primes);
      gtk_switch_set_state (self->sw_work, TRUE);
    }
  else
    {
      gtk_widget_set_sensitive (GTK_WIDGET (toggle), FALSE);
      g_cancellable_cancel (self->work_cancel);
    }

  return TRUE;
}


