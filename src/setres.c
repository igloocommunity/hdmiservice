/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Per Persson per.xb.persson@stericsson.com for
 * ST-Ericsson.
 * License terms: <FOSS license>.
 */

#include <unistd.h>     /* Symbolic Constants */
#include <sys/types.h>  /* Primitive System Data Types */
#include <linux/types.h>
#include <sys/ioctl.h>
#include <errno.h>      /* Errors */
#include <stdarg.h>
#include <stdio.h>      /* Input/Output */
#include <stdlib.h>     /* General Utilities */
#include <string.h>     /* String handling */
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include "linux/fb.h"
#ifdef ANDROID
#include <utils/Log.h>
#endif
#include "../include/hdmi_service_api.h"
#include "../include/hdmi_service_local.h"

/* List of cea numbers. First ceanr has highest priority */
struct vesacea vesaceaprio[CEAPRIO_MAX_SIZE];

/*
 * During edid_parse, sink_support will be filled in.
 * The first format in this list with sink_support set will be chosen.
 */
int video_formats_nr;
struct video_format video_formats[FORMATS_MAX];

int video_formats_clear(void)
{
	memset(video_formats, 0, sizeof(video_formats));
	return 0;
}

int vesacea_supported(int *nr, struct vesacea vesacea[])
{
	int index;

	*nr = 0;
	LOGHDMILIB2("%s begin", __func__);
	for (index = 0; index < FORMATS_MAX; index++) {
		if (video_formats[index].sink_support) {
			vesacea[*nr].cea = video_formats[index].cea;
			vesacea[*nr].nr = video_formats[index].vesaceanr;
			LOGHDMILIB2("cea:%d nr:%d", vesacea[*nr].cea,
							vesacea[*nr].nr);
			(*nr)++;
		}
	}
	LOGHDMILIB2("%s end", __func__);
	return 0;
}

int video_formats_supported_hw(void)
{
	int res;
	int index;
	int vesacea;
	char buf[FORMATS_MAX * 2 + 1];

	/* Get hw supported formats */
	vesacea = dispdevice_file_open(VESACEAFORMATS_FILE, O_RDONLY);
	if (vesacea < 0) {
		LOGHDMILIB("***** Failed to open %s *****",
					VESACEAFORMATS_FILE);
		return -1;
	}

	res = read(vesacea, buf, sizeof(buf));
	close(vesacea);
	if (res <= 0) {
		LOGHDMILIB("***** Failed to read %s *****",
					VESACEAFORMATS_FILE);
		return -1;
	}

	for (index = 0; index < FORMATS_MAX; index++) {
		if ((index * 2 + 2) > res) {
			/* No more to read */
			video_formats[index].cea = 0;
			video_formats[index].vesaceanr = 0;
			video_formats[index].sink_support = 0;
			video_formats[index].prio = VESACEAPRIO_DEFAULT;
			break;
		}
		video_formats[index].cea = *(buf + index * 2);
		video_formats[index].vesaceanr = *(buf + index * 2 + 1);
		video_formats[index].sink_support = 0;
		video_formats[index].prio = VESACEAPRIO_DEFAULT;
	}
	video_formats_nr = index;
	return 0;
}

int nr_formats_get(void)
{
	return video_formats_nr;
}

struct video_format *video_formats_get(void)
{
	return video_formats;
}

static int vesaceanrtovar(struct fb_var_screeninfo *var, __u8 cea,
				__u8 vesaceanr, __u8 num_buffers)
{
	int timing;
	int res;
	unsigned int index;
	char buf[128];
	int interlaced;

	/* Request timing info */
	timing = dispdevice_file_open(TIMING_FILE, O_RDWR);
	if (timing < 0) {
		LOGHDMILIB("***** Failed to open %s *****", TIMING_FILE);
		return -1;
	}
	buf[0] = cea;
	buf[1] = vesaceanr;
	res = write(timing, buf, 2);
	if (res <= 0) {
		LOGHDMILIB("***** Failed to write %s *****", TIMING_FILE);
		close(timing);
		return -1;
	}

	lseek(timing, 0, SEEK_SET);
	res = read(timing, buf, sizeof(buf));
	close(timing);
	if (res <= 0) {
		LOGHDMILIB("***** Failed to read %s *****", TIMING_FILE);
		return -1;
	}

	/* Read timing info */
	if (res == TIMING_SIZE) {
		index = 0;
		memcpy(&var->xres, buf + index, 4);
		LOGHDMILIB3("xres:%d", var->xres);
		index += 4;
		memcpy(&var->yres, buf + index, 4);
		LOGHDMILIB3("yres:%d", var->yres);
		index += 4;
		var->xres_virtual = var->xres;
		var->yres_virtual = var->yres * num_buffers;
		memcpy(&var->pixclock, buf + index, 4);
		LOGHDMILIB3("pixclock:%d", var->pixclock);
		index += 4;
		memcpy(&var->left_margin, buf + index, 4);
		LOGHDMILIB3("left_margin:%d", var->left_margin);
		index += 4;
		memcpy(&var->right_margin, buf + index, 4);
		LOGHDMILIB3("right_margin:%d", var->right_margin);
		index += 4;
		memcpy(&var->upper_margin, buf + index, 4);
		LOGHDMILIB3("upper_margin:%d", var->upper_margin);
		index += 4;
		memcpy(&var->lower_margin, buf + index, 4);
		LOGHDMILIB3("lower_margin:%d", var->lower_margin);
		index += 4;
		var->vmode &= ~FB_VMODE_INTERLACED;
		memcpy(&interlaced, buf + index, 4);
		LOGHDMILIB3("vmode:%x", var->vmode);
		var->vmode |= interlaced ? FB_VMODE_INTERLACED :
						FB_VMODE_NONINTERLACED;
		LOGHDMILIB("CEA %d nr %d found\n", cea, vesaceanr);
		return 0;
	}
	return -EINVAL;
}

void vesacea_prio_default(void)
{
	/* 1920x1080P@30 */
	vesaceaprio[0].cea = 1;
	vesaceaprio[0].nr = 34;

	/* 1280x720P@60 */
	vesaceaprio[1].cea = 1;
	vesaceaprio[1].nr = 4;

	/* 1920x1080P@25 */
	vesaceaprio[2].cea = 1;
	vesaceaprio[2].nr = 33;

	/* 1920x1080P@24 */
	vesaceaprio[3].cea = 1;
	vesaceaprio[3].nr = 32;

	/* 1920x1080I@60 */
	vesaceaprio[4].cea = 1;
	vesaceaprio[4].nr = 5;

	/* 1920x1080I@30 */
	vesaceaprio[5].cea = 1;
	vesaceaprio[5].nr = 20;

	/* 1280x720P@50 */
	vesaceaprio[6].cea = 1;
	vesaceaprio[6].nr = 19;

	/* 720x480P@60 */
	vesaceaprio[7].cea = 1;
	vesaceaprio[7].nr = 3;

	/* end of list */
	vesaceaprio[8].cea = 0;
	vesaceaprio[8].nr = 0;
}

static void set_vesacea_prio(__u8 cea, __u8 vesaceanr, __u8 prio)
{
	int nr_formats = sizeof(video_formats)/sizeof(video_formats[0]);
	int index;

	for (index = 0; index < nr_formats; index++) {
		if ((video_formats[index].cea == cea) &
			(video_formats[index].vesaceanr == vesaceanr)) {
			video_formats[index].prio = prio;
			LOGHDMILIB("set_cea_prio %d %d", index, prio);
			break;
		}
	}
}

void set_vesacea_prio_all(void)
{
	int index;

	/* Set cea prio. Continue until prio = 0 or maxsize */
	for (index = 0; index < CEAPRIO_MAX_SIZE; index++) {
		LOGHDMILIB("index:%d cea:%d prio:%d",
				index,
				vesaceaprio[index].cea,
				vesaceaprio[index].nr);
		if (vesaceaprio[index].nr == 0)
			break;

		set_vesacea_prio(vesaceaprio[index].cea,
				vesaceaprio[index].nr,
				index + 1);
	}
}

int get_best_videoformat(__u8 *cea, __u8 *vesaceanr)
{
	int index;
	int nr_formats;
	struct video_format *video_formats;
	__u8 best_prio;
	int best_ceanr;
	int best_vesanr;

	*cea = 1;
	*vesaceanr = VIDEO_FORMAT_DEFAULT;

	nr_formats = nr_formats_get();
	video_formats = video_formats_get();
	best_prio = VESACEAPRIO_DEFAULT + 1;
	best_ceanr = 0;
	best_vesanr = 0;

	/* Choose best video format */
	for (index = 0; index < nr_formats; index++) {
		LOGHDMILIB("test cea:%d nr:%d prio:%d",
				video_formats[index].cea,
				video_formats[index].vesaceanr,
				video_formats[index].prio);
		if (video_formats[index].sink_support == 0)
			/* No sink support, check next format */
			continue;

		if (video_formats[index].prio < best_prio) {
			/* Prio is best */
			*cea = video_formats[index].cea;
			*vesaceanr = video_formats[index].vesaceanr;
			best_prio = video_formats[index].prio;
		} else if (best_prio >= VESACEAPRIO_DEFAULT) {
			/* Prio is not set; check ceanr */
			if (video_formats[index].cea &&
					(video_formats[index].vesaceanr >
							best_ceanr)) {
				/* It is the highest ceanr */
				*cea = 1;
				*vesaceanr = video_formats[index].vesaceanr;
				best_ceanr = *vesaceanr;
			}

			/* If no cea has been chosen, check vesa */
			if ((best_ceanr == 0) &
					(!video_formats[index].cea &&
					(video_formats[index].vesaceanr >
							best_vesanr))) {
				/* It is the highest veasanr */
				*cea = 0;
				*vesaceanr = video_formats[index].vesaceanr;
				best_vesanr = *vesaceanr;
			}
		}
		LOGHDMILIB("cea:%d nr:%d best_prio:%d",
					*cea, *vesaceanr, best_prio);
	}

	return 0;
}

int hdmi_fb_chres(__u8 cea, __u8 vesaceanr)
{
	struct fb_var_screeninfo var;
	int fd;
	char fbname[128];
	char buf[128];
	int read_res;
	int disponoff;
	__u8 num_buffers;

	/* Get fb dev name */
	disponoff = dispdevice_file_open(DISPONOFF_FILE, O_RDONLY);
	if (disponoff < 0) {
		LOGHDMILIB("***** Failed to open %s *****", DISPONOFF_FILE);
		return -1;
	}

	read_res = read(disponoff, buf, sizeof(buf));
	close(disponoff);
	if (read_res <= 0) {
		LOGHDMILIB("***** Failed to read %s *****", DISPONOFF_FILE);
		return -1;
	}

	/* Open fb */
	sprintf(fbname, "%s%s", FBPATH, buf);
	LOGHDMILIB("fbname:%s", fbname);
	fd = open(fbname, O_RDONLY);
	if (fd <= 0) {
		LOGHDMILIB("%s", "***** Open fb failed *****");
		return -2;
	}

	/* Get screen info */
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var)) {
		LOGHDMILIB("%s", "***** FBIOGET_VSCREENINFO failed *****");
		close(fd);
		return -3;
	}

	num_buffers = var.yres_virtual / var.yres;
	/* Convert ceanr to screeninfo */
	vesaceanrtovar(&var, cea, vesaceanr, num_buffers);

	/* Set screen info */
	if (ioctl(fd, FBIOPUT_VSCREENINFO, &var)) {
		LOGHDMILIB("%s", "***** FBIOPUT_VSCREENINFO failed *****");
		close(fd);
		return -4;
	}

	/* Close fb */
	close(fd);
	return 0;
}

int vesaceaprio_set(__u8 len, __u8 *data)
{
	int index;
	int index_last;
	int cnt = 0;

	LOGHDMILIB("%s begin", __func__);

	if (len < CEAPRIO_MAX_SIZE)
		index_last = len;
	else
		index_last = CEAPRIO_MAX_SIZE;

	for (index = 0; index < CEAPRIO_MAX_SIZE; index++) {
		if (index < index_last) {
			vesaceaprio[index].cea = data[index * 2];
			vesaceaprio[index].nr = data[index * 2 + 1];
			LOGHDMILIB("prio:%d cea:%d nr:%d", cnt,
				vesaceaprio[index].cea,
				vesaceaprio[index].nr);
		} else {
			vesaceaprio[index].cea = 0;
			vesaceaprio[index].nr = 0;
			break;
		}
		cnt++;
	}

	LOGHDMILIB("%s end", __func__);
	return 0;
}
