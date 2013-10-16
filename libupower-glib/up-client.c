/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:up-client
 * @short_description: Main client object for accessing the UPower daemon
 *
 * A helper GObject to use for accessing UPower information, and to be notified
 * when it is changed.
 *
 * See also: #UpDevice
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <glib-object.h>

#include "up-client.h"
#include "up-client-glue.h"
#include "up-device.h"

static void	up_client_class_init	(UpClientClass	*klass);
static void	up_client_init		(UpClient	*client);
static void	up_client_finalize	(GObject	*object);

#define UP_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), UP_TYPE_CLIENT, UpClientPrivate))

/**
 * UpClientPrivate:
 *
 * Private #UpClient data
 **/
struct _UpClientPrivate
{
	UpClientGlue		*proxy;
	GPtrArray		*array;
	gboolean		 done_enumerate;
};

enum {
	UP_CLIENT_DEVICE_ADDED,
	UP_CLIENT_DEVICE_CHANGED,
	UP_CLIENT_DEVICE_REMOVED,
	UP_CLIENT_CHANGED,
	UP_CLIENT_LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_DAEMON_VERSION,
	PROP_ON_BATTERY,
	PROP_WARNING_LEVEL,
	PROP_LID_IS_CLOSED,
	PROP_LID_IS_PRESENT,
	PROP_IS_DOCKED,
	PROP_LAST
};

static guint signals [UP_CLIENT_LAST_SIGNAL] = { 0 };
static gpointer up_client_object = NULL;

G_DEFINE_TYPE (UpClient, up_client, G_TYPE_OBJECT)

/*
 * up_client_get_device:
 */
static UpDevice *
up_client_get_device (UpClient *client, const gchar *object_path)
{
	guint i;
	const gchar *object_path_tmp;
	UpDevice *device;
	UpClientPrivate *priv = client->priv;

	for (i=0; i<priv->array->len; i++) {
		device = g_ptr_array_index (priv->array, i);
		object_path_tmp = up_device_get_object_path (device);
		if (g_strcmp0 (object_path_tmp, object_path) == 0)
			return device;
	}
	return NULL;
}

/**
 * up_client_get_devices:
 * @client: a #UpClient instance.
 *
 * Get a copy of the device objects.
 * You must have called up_client_enumerate_devices_sync() before calling this
 * function.
 *
 * Return value: (element-type UpDevice) (transfer full): an array of #UpDevice objects, free with g_ptr_array_unref()
 *
 * Since: 0.9.0
 **/
GPtrArray *
up_client_get_devices (UpClient *client)
{
	g_return_val_if_fail (UP_IS_CLIENT (client), NULL);
	g_return_val_if_fail (client->priv->done_enumerate, NULL);
	return g_ptr_array_ref (client->priv->array);
}

/**
 * up_client_get_daemon_version:
 * @client: a #UpClient instance.
 *
 * Get UPower daemon version.
 *
 * Return value: string containing the daemon version, e.g. 008
 *
 * Since: 0.9.0
 **/
const gchar *
up_client_get_daemon_version (UpClient *client)
{
	g_return_val_if_fail (UP_IS_CLIENT (client), NULL);
	return up_client_glue_get_daemon_version (client->priv->proxy);
}

/**
 * up_client_get_lid_is_closed:
 * @client: a #UpClient instance.
 *
 * Get whether the laptop lid is closed.
 *
 * Return value: %TRUE if lid is closed or %FALSE otherwise.
 *
 * Since: 0.9.0
 */
gboolean
up_client_get_lid_is_closed (UpClient *client)
{
	g_return_val_if_fail (UP_IS_CLIENT (client), FALSE);
	return up_client_glue_get_lid_is_closed (client->priv->proxy);
}

/**
 * up_client_get_lid_is_present:
 * @client: a #UpClient instance.
 *
 * Get whether a laptop lid is present on this machine.
 *
 * Return value: %TRUE if the machine has a laptop lid
 *
 * Since: 0.9.2
 */
gboolean
up_client_get_lid_is_present (UpClient *client)
{
	g_return_val_if_fail (UP_IS_CLIENT (client), FALSE);
	return up_client_glue_get_lid_is_present (client->priv->proxy);
}

/**
 * up_client_get_is_docked:
 * @client: a #UpClient instance.
 *
 * Get whether the machine is docked into a docking station.
 *
 * Return value: %TRUE if the machine is docked
 *
 * Since: 0.9.2
 */
gboolean
up_client_get_is_docked (UpClient *client)
{
	g_return_val_if_fail (UP_IS_CLIENT (client), FALSE);
	return up_client_glue_get_is_docked (client->priv->proxy);
}

/**
 * up_client_get_on_battery:
 * @client: a #UpClient instance.
 *
 * Get whether the system is running on battery power.
 *
 * Return value: TRUE if the system is currently running on battery, FALSE other wise.
 *
 * Since: 0.9.0
 **/
gboolean
up_client_get_on_battery (UpClient *client)
{
	g_return_val_if_fail (UP_IS_CLIENT (client), FALSE);
	return up_client_glue_get_on_battery (client->priv->proxy);
}

/**
 * up_client_get_warning_level:
 * @client: a #UpClient instance.
 *
 * Get whether the system is low on battery, or critically low.
 *
 * Return value: The #UpDeviceLevel of the whole system.
 *
 * Since: 1.0
 **/
UpDeviceLevel
up_client_get_warning_level (UpClient *client)
{
	g_return_val_if_fail (UP_IS_CLIENT (client), FALSE);
	return up_client_glue_get_warning_level (client->priv->proxy);
}

/*
 * up_client_add:
 */
static void
up_client_add (UpClient *client, const gchar *object_path)
{
	UpDevice *device = NULL;
	UpDevice *device_tmp;
	gboolean ret;

	/* check existing list for this object path */
	device_tmp = up_client_get_device (client, object_path);
	if (device_tmp != NULL) {
		g_warning ("already added: %s", object_path);
		goto out;
	}

	/* create new device */
	device = up_device_new ();
	ret = up_device_set_object_path_sync (device, object_path, NULL, NULL);
	if (!ret)
		goto out;

	/* add to array */
	g_ptr_array_add (client->priv->array, g_object_ref (device));
	g_signal_emit (client, signals [UP_CLIENT_DEVICE_ADDED], 0, device);
out:
	if (device != NULL)
		g_object_unref (device);
}

/*
 * up_client_notify_cb:
 */
static void
up_client_notify_cb (GObject    *gobject,
		     GParamSpec *pspec,
		     UpClient   *client)
{
	/* Proxy the notification from the D-Bus glue object
	 * to the real one */
	g_object_notify (G_OBJECT (client), pspec->name);
}

/*
 * up_client_added_cb:
 */
static void
up_device_added_cb (UpClientGlue *proxy, const gchar *object_path, UpClient *client)
{
	up_client_add (client, object_path);
}

/*
 * up_client_changed_cb:
 */
static void
up_device_changed_cb (UpClientGlue *proxy, const gchar *object_path, UpClient *client)
{
	UpDevice *device;
	device = up_client_get_device (client, object_path);
	if (device != NULL)
		g_signal_emit (client, signals [UP_CLIENT_DEVICE_CHANGED], 0, device);
}

/*
 * up_client_removed_cb:
 */
static void
up_device_removed_cb (UpClientGlue *proxy, const gchar *object_path, UpClient *client)
{
	UpDevice *device;
	device = up_client_get_device (client, object_path);
	if (device != NULL) {
		g_signal_emit (client, signals [UP_CLIENT_DEVICE_REMOVED], 0, device);
		g_ptr_array_remove (client->priv->array, device);
	}
}

/*
 * up_client_changed_cb:
 */
static void
up_client_changed_cb (UpClientGlue *proxy, UpClient *client)
{
	g_signal_emit (client, signals [UP_CLIENT_CHANGED], 0);
}

static void
up_client_get_property (GObject *object,
			 guint prop_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	UpClient *client;
	client = UP_CLIENT (object);

	switch (prop_id) {
	case PROP_DAEMON_VERSION:
		g_value_set_string (value, up_client_glue_get_daemon_version (client->priv->proxy));
		break;
	case PROP_ON_BATTERY:
		g_value_set_boolean (value, up_client_glue_get_on_battery (client->priv->proxy));
		break;
	case PROP_WARNING_LEVEL:
		g_value_set_uint (value, up_client_glue_get_warning_level (client->priv->proxy));
		break;
	case PROP_LID_IS_CLOSED:
		g_value_set_boolean (value, up_client_glue_get_lid_is_closed (client->priv->proxy));
		break;
	case PROP_LID_IS_PRESENT:
		g_value_set_boolean (value, up_client_glue_get_lid_is_present (client->priv->proxy));
		break;
	case PROP_IS_DOCKED:
		g_value_set_boolean (value, up_client_glue_get_is_docked (client->priv->proxy));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * up_client_class_init:
 * @klass: The UpClientClass
 */
static void
up_client_class_init (UpClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = up_client_get_property;
	object_class->finalize = up_client_finalize;

	/**
	 * UpClient:daemon-version:
	 *
	 * The daemon version.
	 *
	 * Since: 0.9.0
	 */
	g_object_class_install_property (object_class,
					 PROP_DAEMON_VERSION,
					 g_param_spec_string ("daemon-version",
							      "Daemon version",
							      NULL,
							      NULL,
							      G_PARAM_READABLE));
	/**
	 * UpClient:on-battery:
	 *
	 * If the computer is on battery power.
	 *
	 * Since: 0.9.0
	 */
	g_object_class_install_property (object_class,
					 PROP_ON_BATTERY,
					 g_param_spec_boolean ("on-battery",
							       "If the computer is on battery power",
							       NULL,
							       FALSE,
							       G_PARAM_READABLE));
	/**
	 * UpClient:warning-level:
	 *
	 * The warning level e.g. %UP_DEVICE_LEVEL_WARNING.
	 *
	 * Since: 1.0
	 **/
	g_object_class_install_property (object_class,
					 PROP_WARNING_LEVEL,
					 g_param_spec_uint ("warning-level",
							    NULL, NULL,
							    UP_DEVICE_LEVEL_UNKNOWN,
							    UP_DEVICE_LEVEL_LAST,
							    UP_DEVICE_LEVEL_UNKNOWN,
							    G_PARAM_READABLE));
	/**
	 * UpClient:lid-is-closed:
	 *
	 * If the laptop lid is closed.
	 *
	 * Since: 0.9.0
	 */
	g_object_class_install_property (object_class,
					 PROP_LID_IS_CLOSED,
					 g_param_spec_boolean ("lid-is-closed",
							       "If the laptop lid is closed",
							       NULL,
							       FALSE,
							       G_PARAM_READABLE));
	/**
	 * UpClient:lid-is-present:
	 *
	 * If a laptop lid is present.
	 *
	 * Since: 0.9.0
	 */
	g_object_class_install_property (object_class,
					 PROP_LID_IS_PRESENT,
					 g_param_spec_boolean ("lid-is-present",
							       "If a laptop lid is present",
							       NULL,
							       FALSE,
							       G_PARAM_READABLE));

	/**
	 * UpClient:is-docked:
	 *
	 * If the laptop is docked
	 *
	 * Since: 0.9.8
	 */
	g_object_class_install_property (object_class,
					 PROP_IS_DOCKED,
					 g_param_spec_boolean ("is-docked",
							       "If a laptop is docked",
							       NULL,
							       FALSE,
							       G_PARAM_READABLE));

	/**
	 * UpClient::device-added:
	 * @client: the #UpClient instance that emitted the signal
	 * @device: the #UpDevice that was added.
	 *
	 * The ::device-added signal is emitted when a power device is added.
	 *
	 * Since: 0.9.0
	 **/
	signals [UP_CLIENT_DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (UpClientClass, device_added),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, UP_TYPE_DEVICE);

	/**
	 * UpClient::device-removed:
	 * @client: the #UpClient instance that emitted the signal
	 * @device: the #UpDevice that was removed.
	 *
	 * The ::device-added signal is emitted when a power device is removed.
	 *
	 * Since: 0.9.0
	 **/
	signals [UP_CLIENT_DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (UpClientClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, UP_TYPE_DEVICE);

	/**
	 * UpClient::device-changed:
	 * @client: the #UpClient instance that emitted the signal
	 * @device: the #UpDevice that was changed.
	 *
	 * The ::device-changed signal is emitted when a power device is changed.
	 *
	 * Since: 0.9.0
	 **/
	signals [UP_CLIENT_DEVICE_CHANGED] =
		g_signal_new ("device-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (UpClientClass, device_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, UP_TYPE_DEVICE);

	/**
	 * UpClient::changed:
	 * @client: the #UpClient instance that emitted the signal
	 *
	 * The ::changed signal is emitted when properties may have changed.
	 *
	 * Since: 0.9.0
	 **/
	signals [UP_CLIENT_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (UpClientClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (UpClientPrivate));
}

/**
 * up_client_enumerate_devices_sync:
 * @client: a #UpClient instance.
 * @error: a #GError, or %NULL.
 *
 * Enumerates all the devices from the daemon.
 *
 * Return value: %TRUE for success, else %FALSE.
 *
 * Since: 0.9.0
 **/
gboolean
up_client_enumerate_devices_sync (UpClient *client, GCancellable *cancellable, GError **error)
{
	const gchar *object_path;
	char **devices;
	guint i;
	gboolean ret = TRUE;

	/* already done */
	if (client->priv->done_enumerate)
		goto out;

	/* coldplug */
	if (up_client_glue_call_enumerate_devices_sync (client->priv->proxy,
							&devices,
							NULL,
							error) == FALSE) {
		ret = FALSE;
		goto out;
	}
	for (i = 0; devices[i] != NULL; i++) {
		object_path = (const gchar *) devices[i];
		up_client_add (client, object_path);
	}

	/* only do this once per instance */
	client->priv->done_enumerate = TRUE;
out:
	return ret;
}

/*
 * up_client_init:
 * @client: This class instance
 */
static void
up_client_init (UpClient *client)
{
	GError *error = NULL;
	GParamSpec **specs;
	guint n_props;
	guint i;

	client->priv = UP_CLIENT_GET_PRIVATE (client);
	client->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* connect to main interface */
	client->priv->proxy = up_client_glue_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
								     G_DBUS_PROXY_FLAGS_NONE,
								     "org.freedesktop.UPower",
								     "/org/freedesktop/UPower",
								     NULL,
								     &error);
	if (client->priv->proxy == NULL) {
		g_warning ("Couldn't connect to proxy: %s", error->message);
		g_error_free (error);
		return;
	}

	/* all callbacks */
	g_signal_connect (client->priv->proxy, "device-added",
			  G_CALLBACK (up_device_added_cb), client);
	g_signal_connect (client->priv->proxy, "device-removed",
			  G_CALLBACK (up_device_removed_cb), client);
	g_signal_connect (client->priv->proxy, "device-changed",
			  G_CALLBACK (up_device_changed_cb), client);
	g_signal_connect (client->priv->proxy, "changed",
			  G_CALLBACK (up_client_changed_cb), client);

	/* Proxy all the property notifications from the glue object */
	specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (client), &n_props);
	for (i = 0; i < n_props; i++) {
		gchar *signal_name;

		signal_name = g_strdup_printf ("notify::%s", specs[i]->name);
		g_signal_connect (client->priv->proxy, signal_name,
				  G_CALLBACK (up_client_notify_cb), client);
		g_free (signal_name);
	}
	g_free (specs);
}

/*
 * up_client_finalize:
 */
static void
up_client_finalize (GObject *object)
{
	UpClient *client;

	g_return_if_fail (UP_IS_CLIENT (object));

	client = UP_CLIENT (object);

	g_ptr_array_unref (client->priv->array);

	if (client->priv->proxy != NULL)
		g_object_unref (client->priv->proxy);

	G_OBJECT_CLASS (up_client_parent_class)->finalize (object);
}

/**
 * up_client_new:
 *
 * Creates a new #UpClient object.
 *
 * Return value: a new UpClient object.
 *
 * Since: 0.9.0
 **/
UpClient *
up_client_new (void)
{
	if (up_client_object != NULL) {
		g_object_ref (up_client_object);
	} else {
		up_client_object = g_object_new (UP_TYPE_CLIENT, NULL);
		g_object_add_weak_pointer (up_client_object, &up_client_object);
	}
	return UP_CLIENT (up_client_object);
}

