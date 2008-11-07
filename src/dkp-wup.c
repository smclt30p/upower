/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2008 Richard Hughes <richard@hughsie.com>
 *
 * Data values taken from wattsup.c: Copyright (C) 2005 Patrick Mochel
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <math.h>

#include <glib.h>
#include <glib-object.h>
#include <devkit-gobject.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <ctype.h>
#include <getopt.h>

#include "sysfs-utils.h"
#include "egg-debug.h"
#include "egg-string.h"

#include "dkp-enum.h"
#include "dkp-object.h"
#include "dkp-wup.h"

#define DKP_WUP_REFRESH_TIMEOUT			1
#define DKP_WUP_COMMAND_CLEAR			"#R,W,0;"
#define DKP_WUP_COMMAND_READ_CAL		"#F,R,0;"
/* Response:
 *	0x0	flags
 *	0x1	sample count
 *	0x2	volts gain
 *	0x3	volts bias
 *	0x4	amps gain
 *	0x5	amps bias
 *	0x6	amps offset
 *	0x7	low amps gain
 *	0x8	low amps bias
 *	0x9	low amps offset
 *	0xa	watts gain
 *	0xb	watts offset
 *	0xc	low watts gain
 *	0xd	low watts offset
 */
#define DKP_WUP_COMMAND_START_LOG		"#L,W,3,E,1,1;"
#define DKP_WUP_COMMAND_STOP_LOG		"#L,R,0;"
/* Response:
 *	0x0	time stamp
 *	0x1	interval
 */
#define DKP_WUP_COMMAND_READ_INTERVAL		"#S,R,0;"
/* Response:
 *	0x0	reserved
 *	0x1	interval
 */
#define DKP_WUP_COMMAND_WRITE_INTERVAL		"#S,W,2," /* {seconds},{interval} */
#define DKP_WUP_COMMAND_READ_MODE		"#M,R,0;"
/* Response:
 *	0x0	display mode
 */
#define DKP_WUP_COMMAND_WRITE_MODE		"#M,W,1," /* {mode} */
#define DKP_WUP_COMMAND_READ_USER		"#U,R,0;"
/* Response:
 *	0x0	cost per kWh
 *	0x1	2nd tier cost
 *	0x2	2nd tier threshold
 *	0x3	duty cycle threshold
 */
#define DKP_WUP_COMMAND_WRITE_USER		"#U,W,0;"
/* Response:
 *	0x0	kwh_cost
 *	0x1	2nd_tier_cost
 *	0x2	2nd_tier_threshold
 *	0x3	duty_cycle_threshold
 */
#define DKP_WUP_COMMAND_READ_HEADER		"#H,R,0;"
/* Response:
 *	0x?	One of the DKP_WUP_RESPONSE_HEADER_x constants */

#define DKP_WUP_RESPONSE_HEADER_WATTS		0x0
#define DKP_WUP_RESPONSE_HEADER_VOLTS		0x1
#define DKP_WUP_RESPONSE_HEADER_AMPS		0x2
#define DKP_WUP_RESPONSE_HEADER_KWH		0x3
#define DKP_WUP_RESPONSE_HEADER_COST		0x4
#define DKP_WUP_RESPONSE_HEADER_MONTHLY_KWH	0x5
#define DKP_WUP_RESPONSE_HEADER_MONTHLY_COST	0x6
#define DKP_WUP_RESPONSE_HEADER_MAX_WATTS	0x7
#define DKP_WUP_RESPONSE_HEADER_MAX_VOLTS	0x8
#define DKP_WUP_RESPONSE_HEADER_MAX_AMPS	0x9
#define DKP_WUP_RESPONSE_HEADER_MIN_WATTS	0xa
#define DKP_WUP_RESPONSE_HEADER_MIN_VOLTS	0xb
#define DKP_WUP_RESPONSE_HEADER_MIN_AMPS	0xc
#define DKP_WUP_RESPONSE_HEADER_POWER_FACTOR	0xd
#define DKP_WUP_RESPONSE_HEADER_DUTY_CYCLE	0xe
#define DKP_WUP_RESPONSE_HEADER_POWER_CYCLE	0xf

/* commands can never be bigger then this */
#define DKP_WUP_COMMAND_LEN			256

struct DkpWupPrivate
{
	guint			 poll_timer_id;
	int			 fd;
};

static void	dkp_wup_class_init	(DkpWupClass	*klass);

G_DEFINE_TYPE (DkpWup, dkp_wup, DKP_TYPE_DEVICE)
#define DKP_WUP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DKP_TYPE_WUP, DkpWupPrivate))

static gboolean		 dkp_wup_refresh	 	(DkpDevice *device);

/**
 * dkp_wup_poll_cb:
 **/
static gboolean
dkp_wup_poll_cb (DkpWup *wup)
{
	gboolean ret;
	DkpDevice *device = DKP_DEVICE (wup);
	DkpObject *obj = dkp_device_get_obj (device);

	egg_debug ("Polling: %s", obj->native_path);
	ret = dkp_wup_refresh (device);
	if (ret)
		dkp_device_emit_changed (device);
	return TRUE;
}

/**
 * dkp_wup_set_speed:
 **/
static gboolean
dkp_wup_set_speed (DkpWup *wup)
{
	struct termios t;
	int retval;

	retval = tcgetattr (wup->priv->fd, &t);
	if (retval != 0) {
		egg_debug ("failed to get speed");
		return FALSE;
	}

	cfmakeraw (&t);
	cfsetispeed (&t, B115200);
	cfsetospeed (&t, B115200);
	tcflush (wup->priv->fd, TCIFLUSH);

	t.c_iflag |= IGNPAR;
	t.c_cflag &= ~CSTOPB;
	retval = tcsetattr (wup->priv->fd, TCSANOW, &t);
	if (retval != 0) {
		egg_debug ("failed to set speed");
		return FALSE;
	}

	return TRUE;
}

/**
 * dkp_wup_write_command:
 *
 * data: a command string in the form "#command,subcommand,datalen,data[n]", e.g. "#R,W,0"
 **/
static gboolean
dkp_wup_write_command (DkpWup *wup, const gchar *data)
{
	guint ret = TRUE;
	int retval;
	gint length;

	length = strlen (data);
	egg_debug ("writing [%s]", data);
	retval = write (wup->priv->fd, data, length);
	if (retval != length) {
		egg_debug ("Writing [%s] to device failed", data);
		ret = FALSE;
	}
	return ret;
}

/**
 * dkp_wup_read_command:
 *
 * Return value: Some data to parse
 **/
static gchar *
dkp_wup_read_command (DkpWup *wup)
{
	int retval;
	gchar buffer[DKP_WUP_COMMAND_LEN];
	retval = read (wup->priv->fd, &buffer, DKP_WUP_COMMAND_LEN);
	if (retval < 0) {
		egg_debug ("failed to read from fd");
		return NULL;
	}
	egg_debug ("reading [%s]", buffer);
	return g_strdup (buffer);
}

/**
 * dkp_wup_parse_command:
 *
 * Return value: Som data to parse
 **/
static gboolean
dkp_wup_parse_command (DkpWup *wup, const gchar *data)
{
	gboolean ret = FALSE;
	gchar command;
	gchar subcommand;
	gchar **tokens = NULL;
	gchar *packet = NULL;
	guint i;
	guint size;
	guint length;
	guint length_tok;
	DkpDevice *device = DKP_DEVICE (wup);
	DkpObject *obj = dkp_device_get_obj (device);
	const guint offset = 3;

	/* invalid */
	if (data == NULL)
		goto out;

	/* Try to find a valid packet in the data stream
	 * Data may be sdfsd#P,-,0;sdfs and we only want this bit:
	 *                  \-----/
	 * so try to find the start and the end */

	/* strip to the first '#' char */
	length = strlen (data);
	for (i=0; i<length-1; i++)
		if (data[i] == '#')
			packet = g_strdup (data+i);

	/* does packet exist? */
	if (packet == NULL) {
		egg_debug ("no start char in %s", data);
		goto out;
	}

	/* replace the first ';' char with a NULL if it exists */
	length = strlen (packet);
	for (i=0; i<length; i++) {
		if (packet[i] == ';') {
			packet[i] = '\0';
			break;
		}
	}

	/* check we have enough data inthe packet */
	tokens = g_strsplit (packet, ",", -1);
	length = g_strv_length (tokens);
	if (length < 3) {
		egg_debug ("not enough tokens '%s'", packet);
		goto out;
	}

	/* remove leading or trailing whitespace in tokens */
	for (i=0; i<length; i++)
		g_strstrip (tokens[i]);

	/* check the first token */
	length_tok = strlen (tokens[0]);
	if (length_tok != 2) {
		egg_debug ("expected command '#?' but got '%s'", tokens[0]);
		goto out;
	}
	if (tokens[0][0] != '#') {
		egg_debug ("expected command '#?' but got '%s'", tokens[0]);
		goto out;
	}
	command = tokens[0][1];

	/* check the second token */
	length_tok = strlen (tokens[1]);
	if (length_tok != 1) {
		egg_debug ("expected command '?' but got '%s'", tokens[1]);
		goto out;
	}
	subcommand = tokens[1][0]; /* expect to be '-' */

	/* check the length */
	size = atoi (tokens[2]);
	if (size != length - offset) {
		egg_debug ("size expected to be '%i' but got '%i'", length - offset, size);
		goto out;
	}

#if 0
	/* print the data */
	egg_debug ("command=%c:%c", command, subcommand);
	for (i=0; i<size; i++) {
		egg_debug ("%i\t'%s'", i, tokens[i+offset]);
	}
#endif

	/* update the command fields */
	if (command == 'd' && subcommand == '-') {
		obj->energy_rate = strtod (tokens[offset+DKP_WUP_RESPONSE_HEADER_WATTS], NULL) / 10.0f;
		obj->voltage = strtod (tokens[offset+DKP_WUP_RESPONSE_HEADER_VOLTS], NULL) / 10.0f;
		ret = TRUE;
	} else {
		egg_debug ("ignoring command '%c'", command);
	}

out:
	g_free (packet);
	g_strfreev (tokens);
	return ret;
}

/**
 * dkp_wup_coldplug:
 **/
static gboolean
dkp_wup_coldplug (DkpDevice *device)
{
	DkpWup *wup = DKP_WUP (device);
	DevkitDevice *d;
	gboolean ret = FALSE;
	const gchar *device_file;
	const gchar *type;
	gchar *data;
	DkpObject *obj = dkp_device_get_obj (device);

	/* detect what kind of device we are */
	d = dkp_device_get_d (device);
	if (d == NULL)
		egg_error ("could not get device");

	/* get the type */
	type = devkit_device_get_property (d, "DKP_MONITOR_TYPE");
	if (type == NULL || !egg_strequal (type, "wup")) {
		egg_debug ("not a Watts Up? Pro device");
		goto out;
	}

	/* get the device file */
	device_file = devkit_device_get_device_file (d);
	if (device_file == NULL) {
		egg_debug ("could not get device file for WUP device");
		goto out;
	}
	egg_debug ("wanted %s", device_file);
	device_file = g_strdup ("/dev/ttyUSB0");
	egg_debug ("opened %s", device_file);

	/* connect to the device */
	wup->priv->fd = open (device_file, O_RDWR | O_NONBLOCK);
	if (wup->priv->fd < 0) {
		egg_debug ("cannot open device file %s", device_file);
		goto out;
	}
	egg_debug ("opened %s", device_file);

	/* set speed */
	ret = dkp_wup_set_speed (wup);
	if (!ret) {
		egg_debug ("not a WUP device (cannot set speed): %s", device_file);
		goto out;
	}

	/* attempt to clear */
	ret = dkp_wup_write_command (wup, DKP_WUP_COMMAND_CLEAR);
	ret = dkp_wup_write_command (wup, DKP_WUP_COMMAND_START_LOG);
	/* dummy read */
	data = dkp_wup_read_command (wup);
	egg_debug ("data after clear %s", data);

	/* shouldn't do anything */
	dkp_wup_parse_command (wup, data);
	g_free (data);

	/* hardcode some values */
	obj->type = DKP_DEVICE_TYPE_MONITOR;
	obj->is_rechargeable = FALSE;
	obj->power_supply = FALSE;
	obj->is_present = FALSE;
	obj->vendor = g_strdup (devkit_device_get_property (d, "ID_VENDOR"));
	obj->model = g_strdup (devkit_device_get_property (d, "ID_PRODUCT"));
	obj->serial = g_strstrip (sysfs_get_string (obj->native_path, "serial"));
	obj->has_history = TRUE;
	obj->state = DKP_DEVICE_STATE_DISCHARGING;

	/* coldplug */
	egg_debug ("coldplug");
	ret = dkp_wup_refresh (device);
	ret = TRUE;

out:
	return ret;
}

/**
 * dkp_wup_refresh:
 **/
static gboolean
dkp_wup_refresh (DkpDevice *device)
{
	gboolean ret = FALSE;
	GTimeVal time;
	gchar *data = NULL;
	DkpWup *wup = DKP_WUP (device);
	DkpObject *obj = dkp_device_get_obj (device);

	/* reset time */
	g_get_current_time (&time);
	obj->update_time = time.tv_sec;

	/* get data */
	data = dkp_wup_read_command (wup);
	if (data == NULL) {
		egg_debug ("no data");
		goto out;
	}

	/* parse */
	ret = dkp_wup_parse_command (wup, data);
	if (!ret) {
		egg_debug ("failed to parse %s", data);
		goto out;
	}

out:
	g_free (data);
	return TRUE;
}

/**
 * dkp_wup_init:
 **/
static void
dkp_wup_init (DkpWup *wup)
{
	wup->priv = DKP_WUP_GET_PRIVATE (wup);
	wup->priv->fd = -1;
	wup->priv->poll_timer_id = g_timeout_add_seconds (DKP_WUP_REFRESH_TIMEOUT,
							  (GSourceFunc) dkp_wup_poll_cb, wup);
}

/**
 * dkp_wup_finalize:
 **/
static void
dkp_wup_finalize (GObject *object)
{
	DkpWup *wup;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DKP_IS_WUP (object));

	wup = DKP_WUP (object);
	g_return_if_fail (wup->priv != NULL);

	if (wup->priv->fd > 0)
		close (wup->priv->fd);
	if (wup->priv->poll_timer_id > 0)
		g_source_remove (wup->priv->poll_timer_id);

	G_OBJECT_CLASS (dkp_wup_parent_class)->finalize (object);
}

/**
 * dkp_wup_class_init:
 **/
static void
dkp_wup_class_init (DkpWupClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	DkpDeviceClass *device_class = DKP_DEVICE_CLASS (klass);

	object_class->finalize = dkp_wup_finalize;
	device_class->coldplug = dkp_wup_coldplug;
	device_class->refresh = dkp_wup_refresh;

	g_type_class_add_private (klass, sizeof (DkpWupPrivate));
}

/**
 * dkp_wup_new:
 **/
DkpWup *
dkp_wup_new (void)
{
	return g_object_new (DKP_TYPE_WUP, NULL);
}

