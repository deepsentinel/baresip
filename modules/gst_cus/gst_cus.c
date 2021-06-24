/**
 * @file gst/gst.c  Gstreamer 1.0 playbin pipeline
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */
#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 199309L
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gst/gst.h>
#include <unistd.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "gst_cus.h"

/**
 * @defgroup gst_cus gst_cus
 *
 * Audio source module using gstreamer 1.0 as input
 *
 * The module 'gst' is using the Gstreamer framework to play external
 * media and provide this as an internal audio source.
 *
 * Example config:
 \verbatim
  audio_source        gst_cus,http://relay.slayradio.org:8000/
 \endverbatim
 */


/**
 * Defines the Gstreamer state
 *
 * <pre>
 *                ptime=variable             ptime=20ms
 *  .-----------. N kHz          .---------. N kHz
 *  |           | 1-2 channels   |         | 1-2 channels
 *  | Gstreamer |--------------->|Packetize|-------------> [read handler]
 *  |           |                |         |
 *  '-----------'                '---------'
 *
 * </pre>
 */
static struct ausrc *ausrc;
static struct auplay *auplay;

static int mod_gst_cus_init(void)
{
	gchar *s;
	int err;

	gst_init(0, NULL);

	s = gst_version_string();

	info("gst: init: %s\n", s);

	g_free(s);
	err = ausrc_register(&ausrc, baresip_ausrcl(),
				   "gst_cus", gst_cus_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       "gst_cus", gst_cus_play_alloc);
	return err;
}


static int mod_gst_cus_close(void)
{
	gst_deinit();
	ausrc = mem_deref(ausrc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(gst) = {
	"gst_cus",
	"sound",
	mod_gst_cus_init,
	mod_gst_cus_close
};
