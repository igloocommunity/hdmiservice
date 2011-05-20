/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Per Persson per.xb.persson@stericsson.com for
 * ST-Ericsson.
 * License terms: <FOSS license>.
 */

#include <unistd.h>     /* Symbolic Constants */
#include <sys/types.h>  /* Primitive System Data Types */
#include <errno.h>      /* Errors */
#include <stdarg.h>
#include <stdio.h>      /* Input/Output */
#include <stdlib.h>     /* General Utilities */
#include <pthread.h>    /* POSIX Threads */
#include <string.h>     /* String handling */
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <sys/poll.h>
#include <utils/Log.h>
#include "../include/hdmi_service_api.h"
#include "../include/hdmi_service_local.h"

static int hdmieventfile_open(struct pollfd *pollfds)
{

	pollfds->fd = open(EVENT_FILE, O_RDONLY);
	if (pollfds->fd  < 0) {
		LOGHDMILIB(" failed to open %s", EVENT_FILE);
		return -1;
	}

	pollfds->events = POLLERR | POLLPRI;
	return 0;
}

static int hdmieventfile_read(int fd)
{
	int read_res;
	char buf[128];
	int events;

	read_res = read(fd, buf, sizeof(buf));
	/* seek back so we can read the new state */
	lseek(fd, 0, SEEK_SET);

	LOGHDMILIB2("read_res:%d", read_res);

	if (read_res != POLL_READ_SIZE)
		return HDMIEVENT_POLLSIZEFAIL;

	events = *buf;

	LOGHDMILIB2("events read:%02x", events);
	return events;
}

static int hdmieventfile_poll(struct pollfd *pollfds)
{
	int result;
	int timeout = -1;	/* Timeout in msec. */

	result = poll(pollfds, 1, timeout);
	switch (result) {
	case 0:
		LOGHDMILIB2("%s", "timeout");
		break;
	case -1:
		LOGHDMILIB2("%s", "poll error");
		break;
	default:
		if (pollfds->revents & POLLERR)
			LOGHDMILIB2("res:%d poll done", result);
		else {
			LOGHDMILIB2("rev:%x res:%d poll done2",
						pollfds->revents, result);
		}
		break;
	}
	return 0;
}

static int hdmieventfile_close(int fd)
{
	LOGHDMILIB("%s", __func__);
	close(fd);
	return 0;
}

int hdmievclr(__u8 mask)
{
	int evclrfd;

	evclrfd = open(EVENTCLR_FILE, O_WRONLY);
	if (evclrfd < 0) {
		LOGHDMILIB(" failed to open %s", EVENTCLR_FILE);
		return -1;
	}
	write(evclrfd, &mask, 1);
	close(evclrfd);
	return 0;
}

/*
 * Reading of kernel events
 */
void thread_kevent_fn(void *arg)
{
	int event;
	struct pollfd pollfds;

	LOGHDMILIB("%s begin", __func__);

	/*
	 * Note: events are subscribed at call to api function hdmi_enable
	 * Until then no events will occur.
	 */

	/* Open the event file */
	hdmieventfile_open(&pollfds);

	while (1) {
		/* Read poll event file */
		event = hdmieventfile_read(pollfds.fd);
		LOGHDMILIB("kevent:%x", event);

		if (event == HDMIEVENT_POLLSIZEFAIL) {
			usleep(100000);

			/* Close event file */
			hdmieventfile_close(pollfds.fd);

			/* Open the event file */
			hdmieventfile_open(&pollfds);

		} else {
			/* Signal main thread */
			hdmi_event(event);

			if (event & HDMIEVENT_WAKEUP)
				break;

		}

		/* Poll plug event file */
		hdmieventfile_poll(&pollfds);
	}

	/* Clear events */
	hdmievclr(EVENTMASK_ALL);

	/* Close event file */
	hdmieventfile_close(pollfds.fd);

	LOGHDMILIB("%s end", __func__);

	/* Exit thread */
	pthread_exit(NULL);
}
