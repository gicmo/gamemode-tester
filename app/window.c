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

#include <gio/gio.h>

struct _GmtWindow
{
  GtkApplicationWindow parent_instance;

  /* Template widgets */
  GtkHeaderBar *header_bar;
  GtkLabel     *lbl_pid;
  GtkSwitch    *sw_gamemode;

  /*  */
  int      pid;

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

static void     on_builtin_switch_ready (GObject      *source,
                                         GAsyncResult *res,
                                         gpointer      user_data);

/* ui signals */
static gboolean on_gamemode_toggled (GmtWindow *self,
                                     gboolean   enable,
                                     GtkSwitch *toggle);


/* private stuff */

static void
gmt_window_class_init (GmtWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/GameModeTester/window.ui");
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, header_bar);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, lbl_pid);
  gtk_widget_class_bind_template_child (widget_class, GmtWindow, sw_gamemode);

  gtk_widget_class_bind_template_callback (widget_class, on_gamemode_toggled);
}

#define GAMEMODE_DBUS_NAME "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_IFACE "com.feralinteractive.GameMode"
#define GAMEMODE_DBUS_PATH "/com/feralinteractive/GameMode"

static void
gmt_window_init (GmtWindow *self)
{
  g_autoptr(GCredentials) creds = NULL;
  g_autofree char *pidstr = NULL;
  GDBusProxyFlags flags;

  gtk_widget_init_template (GTK_WIDGET (self));

  creds = g_credentials_new ();
  self->pid = g_credentials_get_unix_pid (creds, NULL);
  pidstr = g_strdup_printf ("%d", self->pid);
  gtk_label_set_text (self->lbl_pid, pidstr);

  flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION;
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            flags, NULL,
                            GAMEMODE_DBUS_NAME,
                            GAMEMODE_DBUS_PATH,
                            GAMEMODE_DBUS_IFACE,
                            NULL,
                            on_bus_ready,
                            self);
}

static void
on_bus_ready (GObject      *source,
              GAsyncResult *res,
              gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  GmtWindow *self;

  self = GMT_WINDOW (user_data);

  self->gamemode = g_dbus_proxy_new_for_bus_finish (res, &err);

  if (self->gamemode == NULL)
    {
      g_warning ("could not create gamemode proxy: %s", err->message);
      return;
    }

  gtk_widget_set_sensitive (GTK_WIDGET (self->sw_gamemode), TRUE);
}

static gboolean
on_gamemode_toggled (GmtWindow *self,
                     gboolean   enable,
                     GtkSwitch *toggle)
{
  g_debug ("Toggled: %s", (enable ? "enable" : "disable"));

  gtk_widget_set_sensitive (GTK_WIDGET (self->sw_gamemode), FALSE);

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

/* native gamemode implementation */

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
