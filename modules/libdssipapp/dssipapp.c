/**
 * @file src/main.c  Main application code
 *
 * Copyright (C) 2010 - 2021 Alfred E. Heggestad
 */
#ifdef SOLARIS
#define __EXTENSIONS__ 1
#endif
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <re.h>
#include <baresip.h>
#include "dssipapp.h"

static struct call *inner_call;

static void net_change_handler(void *arg)
{
	(void)arg;

	info("IP-address changed: %j\n",
	     net_laddr_af(baresip_network(), AF_INET));

	(void)uag_reset_transp(true, true);
}


static void ua_exit_handler(void *arg)
{
	(void)arg;
	debug("ua exited -- stopping main runloop\n");

	/* The main run-loop can be stopped now */
	re_cancel();
}


static void tmr_quit_handler(void *arg)
{
	(void)arg;

	ua_stop_all(false);
}

int simple_call(const char *uri)
{
	struct mbuf *uribuf = NULL;
	char *complet_uri = NULL;
	//struct call *call;
	struct ua *ua;
	int err = 0;

	ua = uag_find_requri(uri);

	uribuf = mbuf_alloc(64);
	if (!uribuf)
		return ENOMEM;

	err = account_uri_complete(ua_account(ua), uribuf, uri);
	if (err) {
		printf("Error on account_uri_complete %d",err);
		return EINVAL;
	}

	uribuf->pos = 0;
	err = mbuf_strdup(uribuf, &complet_uri, uribuf->end);
	if (err)
		goto out;


	err = ua_connect(ua, &inner_call, NULL, complet_uri, VIDMODE_ON);

	printf("call id: %s\n", call_id(inner_call));

out:
	mem_deref(uribuf);
	mem_deref(complet_uri);
	return err;
}

int simple_hangup(void){
	call_hangup(inner_call, call_scode(inner_call), "");
	return 0;
}

int simple_quit(void){
	ua_stop_all(false);
	return 0;
}


static void *start_sip_instance(void *arg) {
	int af = AF_UNSPEC, run_daemon = false;
	const char *ua_eprm = NULL;
	const char *net_interface = NULL;
	const char *audio_path = NULL;
	const char *modv[16];
	struct tmr tmr_quit;
	bool sip_trace = false;
	size_t modc = 0;
	size_t i;
	uint32_t tmo = 0;
	int err=0;

	(void*)arg;
	/*
	 * turn off buffering on stdout
	 */
	//setbuf(stdout, NULL);

	(void)re_fprintf(stdout, "baresip v%s"
			 " Copyright (C) 2010 - 2021"
			 " Alfred E. Heggestad et al.\n",
			 BARESIP_VERSION);

	(void)sys_coredump_set(true);

	if(strlen(config_path)>0){
		info("config path: %s\n", config_path);
		conf_path_set(config_path);
	}
	else
		warning("Using default path\n");


	err = libre_init();
	if (err)
		goto out;

	tmr_init(&tmr_quit);

	err = conf_configure();
	if (err) {
		warning("main: configure failed: %m\n", err);
		goto out;
	}

	/*
	 * Set the network interface before initializing the config
	 */
	if (net_interface) {
		struct config *theconf = conf_config();

		str_ncpy(theconf->net.ifname, net_interface,
			 sizeof(theconf->net.ifname));
	}

	/*
	 * Set prefer_ipv6 preferring the one given in -6 argument (if any)
	 */
	if (af != AF_UNSPEC)
		conf_config()->net.af = af;

	/*
	 * Initialise the top-level baresip struct, must be
	 * done AFTER configuration is complete.
	*/
	err = baresip_init(conf_config());
	if (err) {
		warning("main: baresip init failed (%m)\n", err);
		goto out;
	}

	/* Set audio path preferring the one given in -p argument (if any) */
	if (audio_path)
		play_set_path(baresip_player(), audio_path);
	else if (str_isset(conf_config()->audio.audio_path)) {
		play_set_path(baresip_player(),
			      conf_config()->audio.audio_path);
	}

	/* NOTE: must be done after all arguments are processed */
	if (modc) {

		info("pre-loading modules: %zu\n", modc);

		for (i=0; i<modc; i++) {

			err = module_preload(modv[i]);
			if (err) {
				re_fprintf(stderr,
					   "could not pre-load module"
					   " '%s' (%m)\n", modv[i], err);
			}
		}
	}

	/* Initialise User Agents */
	err = ua_init("baresip v" BARESIP_VERSION " (" ARCH "/" OS ")",
		      true, true, true);
	if (err)
		goto out;

	net_change(baresip_network(), 60, net_change_handler, NULL);

	uag_set_exit_handler(ua_exit_handler, NULL);

	if (ua_eprm) {
		err = uag_set_extra_params(ua_eprm);
		if (err)
			goto out;
	}

	if (sip_trace)
		uag_enable_sip_trace(true);

	/* Load modules */
	err = conf_modules();
	if (err)
		goto out;

	if (run_daemon) {
		err = sys_daemon();
		if (err)
			goto out;

		log_enable_stdout(false);
	}

	info("baresip is ready.\n");

	if (tmo) {
		tmr_start(&tmr_quit, tmo * 1000, tmr_quit_handler, NULL);
	}

	/* Main loop */
	err = re_main(NULL);

 out:
	tmr_cancel(&tmr_quit);

	if (err)
		ua_stop_all(true);

	ua_close();

	/* note: must be done before mod_close() */
	module_app_unload();

	conf_close();

	baresip_close();

	/* NOTE: modules must be unloaded after all application
	 *       activity has stopped.
	 */
	info("main: unloading modules..\n");
	mod_close();

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	//return err;
	return NULL;
}
int start_sip(const char *path){
	//Let it run in new thread. Otherwirse, it would block python thread
	pthread_t t;

	strcpy(config_path, path);
	pthread_create(&t, NULL, start_sip_instance, NULL);
	return 0;
}