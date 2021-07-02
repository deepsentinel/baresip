#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gst_cus.h"
#define SAMPLE_RATE 8000       /* Samples per second we are sending */

struct auplay_st {
    pthread_t thread;
    volatile bool run;
    gint64 base_time;
    void *sampv;
	size_t sampc;
	struct auplay_prm prm;   //write parameter
	auplay_write_h *wh;      //write handler
	void *arg;               //handler paramter

	/* Gstreamer */
	GstElement *pipeline, *appsrc, *queue, *convert, *resample, *sink;
};

static bool
push_data (struct auframe *af, struct auplay_st *st) {
    GstBuffer *buffer;
    GstFlowReturn ret;
    gint64 current_time = g_get_real_time();  //usec

    int buffer_size = auframe_size(af);
    /* Create a new empty buffer */
    buffer = gst_buffer_new_and_alloc (buffer_size);

    gst_buffer_fill (buffer, 0, af->sampv, buffer_size);

    /* Set its timestamp and duration */
    GST_BUFFER_TIMESTAMP (buffer) = (current_time-st->base_time) * 1000;
    GST_BUFFER_DURATION (buffer) = st->prm.ptime * 1000000;

    /* Push the buffer into the appsrc */
    g_signal_emit_by_name (st->appsrc, "push-buffer", buffer, &ret);

    /* Free the buffer now that we are done with it */
    gst_buffer_unref (buffer);

    if (ret != GST_FLOW_OK) {
        /* We got some error, stop sending data */
        warning("push data failed");
        return FALSE;
    }

    return TRUE;
}

static GstBusSyncReply
sync_handler(
	GstBus * bus, GstMessage * msg,
	struct auplay_st *st)
{
	GstTagList *tag_list;
	gchar *title;
	GError *err;
	gchar *d;

	(void)bus;

	switch (GST_MESSAGE_TYPE(msg)) {

		case GST_MESSAGE_EOS:
			st->run = false;
			return GST_BUS_DROP;

		case GST_MESSAGE_ERROR:
			gst_message_parse_error(msg, &err, &d);

			warning("gst: Error: %d(%m) message=\"%s\"\n",
				err->code,
				err->code, err->message);
			warning("gst: Debug: %s\n", d);

			g_free(d);

			g_error_free(err);

			st->run = false;
			return GST_BUS_DROP;

		case GST_MESSAGE_TAG:
			gst_message_parse_tag(msg, &tag_list);

			if (gst_tag_list_get_string(
				tag_list,
				GST_TAG_TITLE,
				&title)) {

				info("gst: title: %s\n", title);
				g_free(title);
			}
			return GST_BUS_DROP;

		default:
			return GST_BUS_PASS;
	}
}

static int setup_pipeline(struct auplay_st *st) {
    GstBus *bus;
    GstAudioInfo info;
    GstCaps *audio_caps;

	st->pipeline = gst_pipeline_new("pipeline");
	if (!st->pipeline) {
		warning("gst: failed to create pipeline element\n");
		return ENOMEM;
	}
    st->appsrc = gst_element_factory_make ("appsrc", "play_appsrc");
    st->queue = gst_element_factory_make ("queue", NULL);
    st->convert = gst_element_factory_make ("audioconvert", NULL);
    st->resample = gst_element_factory_make ("audioresample", NULL);
    st->sink = gst_element_factory_make ("interaudiosink", NULL);

    gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16LE, st->prm.srate, st->prm.ch, NULL);
    audio_caps = gst_audio_info_to_caps (&info);
    g_object_set (st->appsrc, "caps", audio_caps, "format", GST_FORMAT_TIME, NULL);
    gst_caps_unref (audio_caps);
	g_object_set (st->sink, "channel", "sipoutput", NULL);

    gst_bin_add_many (GST_BIN (st->pipeline),
        st->appsrc, st->queue, st->convert, st->resample, st->sink, NULL);

    if (gst_element_link_many (st->appsrc, st->queue, st->convert, st->resample, st->sink, NULL) != TRUE) {
        warning ("Elements could not be linked.\n");
        gst_object_unref (st->pipeline);
        return ENOEXEC;
    }

    bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));

	gst_bus_enable_sync_message_emission(bus);
	gst_bus_set_sync_handler(
		bus, (GstBusSyncHandler) sync_handler,
		st, NULL);

	gst_object_unref(bus);

    gst_element_set_state(st->pipeline, GST_STATE_PLAYING);
    return 0;
}

static void *write_thread(void *arg)
{
	struct auplay_st *st = arg;
	struct auframe af;

	auframe_init(&af, st->prm.fmt, st->sampv, st->sampc);

	while (st->run) {
		st->wh(&af, st->arg);   //get audio buffer
        push_data(&af, st);
        sys_msleep(st->prm.ptime);  //sleep 20 ms(buffer duration)
	}

	return NULL;
}

static void auplay_destructor(void *arg) {
	struct auplay_st *st = arg;

    st->run = false;
    (void)pthread_join(st->thread, NULL);

    gst_element_set_state(st->pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(st->pipeline));

	mem_deref(st->sampv);
}

int gst_cus_play_alloc(struct auplay_st **stp, const struct auplay *ap,
	       struct auplay_prm *prm, const char *device,
	       auplay_write_h *wh, void *arg) {

	struct auplay_st *st;
	int err;

	if (!stp || !ap || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

    info("gst_cus_play_alloc alloc prm %u %u %u %s \n",
        prm->srate, prm->ch, prm->ptime, aufmt_name(prm->fmt));
	st->prm = *prm;
	st->wh  = wh;
	st->arg = arg;

    st->sampc = prm->srate * prm->ch * prm->ptime / 1000;  //sample count per frame
    st->run = true;
	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
    st->base_time = g_get_real_time();  //usec

	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	err = setup_pipeline(st);

    err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
