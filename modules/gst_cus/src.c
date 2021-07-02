#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 199309L
#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include <unistd.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "gst_cus.h"

struct ausrc_st {
	bool run;                   /**< Running flag            */
	ausrc_read_h *rh;           /**< Read handler            */
	ausrc_error_h *errh;        /**< Error handler           */
	void *arg;                  /**< Handler argument        */
	struct ausrc_prm prm;       /**< Read parameters         */
	struct aubuf *aubuf;        /**< Packet buffer           */
	size_t psize;               /**< Packet size in bytes    */
	size_t sampc;
	uint32_t ptime;

	/* Gstreamer */
	GstElement *pipeline, *src, *caps, *sink;
};

static GstBusSyncReply
sync_handler(
	GstBus * bus, GstMessage * msg,
	struct ausrc_st *st)
{
	GstTagList *tag_list;
	gchar *title;
	GError *err;
	gchar *d;

	(void)bus;

	switch (GST_MESSAGE_TYPE(msg)) {

		case GST_MESSAGE_EOS:
			st->run = false;
			break;

		case GST_MESSAGE_ERROR:
			gst_message_parse_error(msg, &err, &d);

			warning("gst: Error: %d(%m) message=\"%s\"\n",
				err->code,
				err->code, err->message);
			warning("gst: Debug: %s\n", d);

			g_free(d);

			/* Call error handler */
			if (st->errh)
				st->errh(err->code, err->message, st->arg);

			g_error_free(err);

			st->run = false;
			break;

		case GST_MESSAGE_TAG:
			gst_message_parse_tag(msg, &tag_list);

			if (gst_tag_list_get_string(
				tag_list,
				GST_TAG_TITLE,
				&title)) {

				info("gst: title: %s\n", title);
				g_free(title);
			}
			break;

		default:
			break;
	}
	gst_message_unref(msg);
	return GST_BUS_DROP;
}

static void format_check(struct ausrc_st *st, GstStructure *s)
{
	int rate, channels;
	const char *format;

	if (!st || !s)
		return;

	gst_structure_get_int(s, "rate", &rate);
	gst_structure_get_int(s, "channels", &channels);
	format = gst_structure_get_string(s, "format");

	if ((int)st->prm.srate != rate) {
		warning("gst: expected %u Hz (got %u Hz)\n", st->prm.srate,
			rate);
	}
	if (st->prm.ch != channels) {
		warning("gst: expected %d channels (got %d)\n",
			st->prm.ch, channels);
	}
	if (strcmp("S16LE", format)){
		warning("gst: expected format S16LE (got %s)\n", format);
	}
}


static void play_packet(struct ausrc_st *st)
{
	int16_t buf[st->sampc];
	struct auframe af = {
		.fmt   = AUFMT_S16LE,
		.sampv = buf,
		.sampc = st->sampc
	};

	/* timed read from audio-buffer */
	if (st->prm.ptime && aubuf_get_samp(st->aubuf, st->prm.ptime, buf,
				st->sampc))
		return;

	/* immediate read from audio-buffer */
	if (!st->prm.ptime)
		aubuf_read_samp(st->aubuf, buf, st->sampc);

	/* call read handler */
	if (st->rh)
		st->rh(&af, st->arg);
}


/* Expected format: 16-bit signed PCM */
static void packet_handler(struct ausrc_st *st, GstBuffer *buffer)
{
	GstMapInfo info;
	int err;

	if (!st->run)
		return;

	/* NOTE: When streaming from files, the buffer will be filled up
	 *       pretty quickly..
	 */

	if (!gst_buffer_map(buffer, &info, GST_MAP_READ)) {
		warning("gst: gst_buffer_map failed\n");
		return;
	}

	err = aubuf_write(st->aubuf, info.data, info.size);
	if (err) {
		warning("gst: aubuf_write: %m\n", err);
	}

	gst_buffer_unmap(buffer, &info);

	/* Empty buffer now */
	while (st->run) {
		const struct timespec delay = {0, st->prm.ptime*1000000/2};

		play_packet(st);

		if (aubuf_cur_size(st->aubuf) < st->psize)
			break;

		(void)nanosleep(&delay, NULL);
	}
}


static void handoff_handler(GstElement *sink, GstBuffer *buffer,
			    GstPad *pad, gpointer user_data)
{
	struct ausrc_st *st = user_data;
	GstCaps *caps;
	(void)sink;

	caps = gst_pad_get_current_caps(pad);

	format_check(st, gst_caps_get_structure(caps, 0));

	gst_caps_unref(caps);

	packet_handler(st, buffer);
}


static int gst_setup(struct ausrc_st *st)
{
	GstBus *bus;
	GstCaps *caps;

	st->run = true;

	st->pipeline = gst_pipeline_new("pipeline");
	st->src = gst_element_factory_make("interaudiosrc", NULL);
	st->caps = gst_element_factory_make("capsfilter", NULL);
	st->sink = gst_element_factory_make("fakesink", NULL);

	g_object_set(st->src, "channel", "sipinput", NULL);
	g_object_set(st->sink, "async", false, 
						"signal-handoffs", TRUE, NULL);

	caps = gst_caps_new_simple("audio/x-raw",
				"format", G_TYPE_STRING, "S16LE",
				"rate", G_TYPE_INT, st->prm.srate,
				"channels", G_TYPE_INT, st->prm.ch,
				   NULL);
	g_object_set(G_OBJECT(st->caps), "caps", caps, NULL);
	gst_caps_unref(caps);

	gst_bin_add_many(GST_BIN(st->pipeline), st->src, st->caps, st->sink, NULL);
	gst_element_link_many(st->src, st->caps, st->sink, NULL);

	/* Override audio-sink handoff handler */
	g_signal_connect(st->sink, "handoff", G_CALLBACK(handoff_handler), st);

	/* Bus watch */
	bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));

	gst_bus_enable_sync_message_emission(bus);
	gst_bus_set_sync_handler(
		bus, (GstBusSyncHandler) sync_handler,
		st, NULL);

	gst_object_unref(bus);
	return 0;
}

static void gst_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (st->run) {
		st->run = false;
	}

	gst_element_set_state(st->pipeline, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(st->pipeline));

	mem_deref(st->aubuf);
}

int gst_cus_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		     struct media_ctx **ctx,
		     struct ausrc_prm *prm, const char *device,
		     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;
	(void)ctx;

	if (!stp || !as || !prm)
		return EINVAL;

	if (!str_isset(device))
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("gst: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), gst_destructor);
	if (!st)
		return ENOMEM;

	st->rh   = rh;
	st->errh = errh;
	st->arg  = arg;

	st->ptime = prm->ptime;
	if (!st->ptime)
		st->ptime = 20;

	if (!prm->srate)
		prm->srate = 8000;

	if (!prm->ch)
		prm->ch = 1;

	st->prm   = *prm;
	st->sampc = prm->srate * prm->ch * st->ptime / 1000;
	st->psize = 2 * st->sampc;

	err = aubuf_alloc(&st->aubuf, st->psize, 0);
	if (err)
		goto out;

	err = gst_setup(st);
	if (err)
		goto out;

	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
