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
#include <sys/ioctl.h>
#include "linux/fb.h"
#include <sys/socket.h>
#include <utils/Log.h>
#include <dirent.h>
#include "../include/hdmi_service_api.h"
#include "../include/hdmi_service_local.h"

pthread_t thread_main;
pthread_t thread_kevent;
pthread_t thread_socklisten;
pthread_mutex_t event_mutex;
pthread_mutex_t fb_state_mutex;
pthread_mutex_t cmd_mutex;
pthread_cond_t event_cond;
#ifdef HDMI_SERVICE_USE_CALLBACK_FN
void (*hdmi_callback_fn)(int cmd, int data_length, __u8 *data) = NULL;
#endif /*HDMI_SERVICE_USE_CALLBACK_FN*/
int hdmi_events;
enum hdmi_fb_state hdmi_fb_state;
enum hdmi_plug_state hdmi_plug_state = HDMI_PLUGUNDEF;
struct cmd_data *cmd_data;
int cmd_id_ind;
char dispdevice_path[64];

const __u8 plugdetdis_val[] = {0x00, 0x00, 0x00};/* 00: disable, 00:ontime,
								00: offtime*/
const __u8 plugdeten_val[] = {0x01, 0x05, 0x02};/* 01: enable, 05:ontime,
								02: offtime*/

static int hdmidirsort(const struct dirent **a, const struct dirent **b)
{
	return 1;
}

/* Find the correct device path since the device minor number can vary */
static int dispdevice_path_set(void)
{
	struct dirent **namelist;
	int n;
	int found = 0;

	n = scandir(DISPDEVICE_PATH_1, &namelist, 0, hdmidirsort);
	if (n < 0) {
		LOGHDMILIB("scandir error");
		return -1;
	}

	while (n--) {
		if (!found && strncmp(namelist[n]->d_name, DISPDEVICE_PATH_2,
				strlen(DISPDEVICE_PATH_2)) == 0) {
			strcpy(dispdevice_path, namelist[n]->d_name);
			LOGHDMILIB("%s found:%s\n", __func__, dispdevice_path);
			found = 1;
		}
		free(namelist[n]);
	}
	free(namelist);
	return 0;
}

static char *dispdevice_path_get(void)
{
	return &dispdevice_path[0];
}

int dispdevice_file_open(char *file, int attr)
{
	int fd = -1;
	char fname[128];

	if (dispdevice_path[0] == 0)
		dispdevice_path_set();

	if (dispdevice_path[0] != 0) {
		sprintf(fname, "%s%s/%s", DISPDEVICE_PATH_1,
				dispdevice_path_get(), file);
		fd = open(fname, attr);
	}
	return fd;
}

int get_new_cmd_id_ind(void)
{
	cmd_id_ind++;
	return cmd_id_ind;
}

/* Sets the format to be used in sysfs files */
static int storeastext(int as_text)
{
	int storeastext;
	int result = RESULT_OK;
	int wr_res;
	char *str;

	if (as_text)
		str = STOREASTEXT_STR;
	else
		str = STOREASBIN_STR;

	/* Set file format in sysfs files; hextext or binary */
	storeastext = open(STOREASTEXT_FILE, O_WRONLY);
	if (storeastext < 0) {
		LOGHDMILIB("***** Failed to open %s *****", STOREASTEXT_FILE);
		result = SYSFS_FILE_FAILED;
		goto storeastext_end;
	}
	wr_res = write(storeastext, str, strlen(str));
	close(storeastext);
	if (wr_res != (int)strlen(str))
		result = STOREAS_FAIL;

storeastext_end:
	return result;
}

/* Trigger event in kernel event file */
static int hdmievwakeupfile_wr(void)
{
	int evwakeup;
	int res;
	__u8 val = 1;

	evwakeup = open(EVWAKEUP_FILE, O_WRONLY);
	if (evwakeup < 0) {
		LOGHDMILIB("***** Failed to open %s *****", EVWAKEUP_FILE);
		return -1;
	}
	res = write(evwakeup, &val, 1);
	close(evwakeup);
	if (res != 1) {
		LOGHDMILIB("***** Failed to write %s *****", EVWAKEUP_FILE);
		return -2;
	}
	return 0;
}

/* Set hw power */
int poweronoff(__u8 onoff)
{
	int pwrfd;

	pwrfd = open(POWERONOFF_FILE, O_WRONLY);
	if (pwrfd < 0) {
		LOGHDMILIB(" failed to open %s", POWERONOFF_FILE);
		return -1;
	}
	write(pwrfd, &onoff, 1);
	close(pwrfd);

	return 0;
}

/* Get hw power */
static int powerstate_get(enum hdmi_power_state *power_state)
{
	int pwrfd;
	int res;
	__u8 onoff;

	*power_state = HDMI_POWERUNDEF;
	pwrfd = open(POWERONOFF_FILE, O_RDONLY);
	if (pwrfd < 0) {
		LOGHDMILIB(" failed to open %s", POWERONOFF_FILE);
		return -1;
	}
	res = read(pwrfd, &onoff, 1);
	close(pwrfd);
	if (res != 1)
		return -1;
	if (onoff)
		*power_state = HDMI_POWERON;
	else
		*power_state = HDMI_POWEROFF;
	return 0;
}

/* Set local plug state */
static int plugstate_set(enum hdmi_plug_state plug_state)
{
	hdmi_plug_state = plug_state;
	return 0;
}

/* Get local plug state */
static int plugstate_get(enum hdmi_plug_state *plug_state)
{
	*plug_state = hdmi_plug_state;
	return 0;
}

/* Select HDMI or DVI mode */
static int hdmi_format_set(enum hdmi_format format)
{
	int fd;

	fd = dispdevice_file_open(HDMIFORMAT_FILE, O_WRONLY);
	if (fd < 0) {
		LOGHDMILIB(" failed to open %s", HDMIFORMAT_FILE);
		return -1;
	}
	write(fd, &format, 1);
	close(fd);

	return 0;
}

/* Send illegal state message on client socket */
static int illegalstate_send(__u32 cmd,  __u32 cmd_id)
{
	int val;
	__u8 buf[16];
	int sock;

	val = cmd;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	memcpy(&buf[CMDID_OFFSET], &cmd_id, 4);
	val = 0;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);

	/* Send on socket */
	return clientsocket_send(buf, CMDBUF_OFFSET + val);
}

/* Subscribe for plug events */
int hdmiplug_subscribe(void)
{
	int plugdetenfd;
	int res;

	plugdetenfd = open(PLUGDETEN_FILE, O_WRONLY);
	if (plugdetenfd < 0) {
		LOGHDMILIB(" failed to open %s", PLUGDETEN_FILE);
		goto hdmiplug_subscribe_err2;
	}

	/* Subscribe */
	res = write(plugdetenfd, plugdeten_val, sizeof(plugdeten_val));
	if (res != sizeof(plugdeten_val))
		goto hdmiplug_subscribe_err1;

	close(plugdetenfd);
	return 0;

hdmiplug_subscribe_err1:
	close(plugdetenfd);
hdmiplug_subscribe_err2:
	return -1;
}

/* Allow-Avoid Early suspend */
static int stayalive(__u8 enable)
{
	int stayalivefd;
	int cnt = 0;
	int res;

	stayalivefd = dispdevice_file_open(STAYALIVE_FILE, O_WRONLY);
	while ((stayalivefd < 0) && (cnt++ < 30)) {
		usleep(200000);
		stayalivefd = dispdevice_file_open(STAYALIVE_FILE, O_WRONLY);
	}
	LOGHDMILIB("cnt:%d", cnt);

	if (stayalivefd < 0) {
		LOGHDMILIB(" failed to open %s", STAYALIVE_FILE);
		goto stayalive_err2;
	}
	res = write(stayalivefd, &enable, 1);
	if (res != 1)
		goto stayalive_err1;

	close(stayalivefd);
	return 0;

stayalive_err1:
	close(stayalivefd);
stayalive_err2:
	return -1;
}

/* Handling of plug events */
static int hdmiplugged_handle(int *basic_audio_support)
{
	__u8 data[128];
	int nr_formats;
	__u8 cea;
	__u8 vesaceanr;
	int disponoff;
	char req_str[7];
	int wr_res;
	int fd;
	char fbname[128];
	char buf[128];
	int read_res;
	struct video_format *formats;
	__u8 extension;
	int cnt = 0;
	struct edid_latency edid_latency = {-1, -1, -1, -1};
	int res;
	int ret = 0;

	LOGHDMILIB("%s", "HDMIEVENT_HDMIPLUGGED");

	plugstate_set(HDMI_PLUGGED);
	*basic_audio_support = 0;
	video_formats_clear();

	/* Behaviour at early suspend */
	stayalive(HDMI_SERVICE_STAY_ALIVE_DURING_SUSPEND);

	/* Set hdmi fb state */
	hdmi_fb_state = HDMI_FB_OPENED;

	/* Get HW supported formats */
	video_formats_supported_hw();
	nr_formats = nr_formats_get();
	formats = video_formats_get();

	cnt = 0;
	/* Read and parse EDID */
	res = -1;
	while (res && (cnt < 3)) {
		res = edid_read(0, data);
		if (res == 0)
			res = edid_parse0(data + 1, &extension, formats,
						nr_formats);
		if (res && (cnt < 2))
			usleep(EDIDREAD_WAITTIME0);
		cnt++;
	}
	if (res) {
		ret = -1;
		goto hdmiplugged_handle_end;
	}

	if (extension) {
		/* Extension data exists */
		cnt = 0;
		res = -1;
		while (res && (cnt < 3)) {
			res = edid_read(1, data);
			if (res == 0)
				res = edid_parse1(data + 1, formats, nr_formats,
							basic_audio_support,
							&edid_latency);
			if (res && (cnt < 2))
				usleep(EDIDREAD_WAITTIME1);
			cnt++;
		}

		if (res) {
			ret = -1;
			goto hdmiplugged_handle_end;
		}

		/* Set hdmi format to hdmi */
		hdmi_format_set(HDMI_FORMAT_HDMI);
		cea = 1;
	} else {
		/* Set hdmi format to dvi */
		hdmi_format_set(HDMI_FORMAT_DVI);
		cea = 0;
	}

	LOGHDMILIB("Basic audio support: %d", *basic_audio_support);
	LOGHDMILIB("Latency: video:%d audio:%d",
			edid_latency.video_latency,
			edid_latency.audio_latency);
	LOGHDMILIB("Interlaced latency: video:%d audio:%d",
			edid_latency.intlcd_video_latency,
			edid_latency.intlcd_audio_latency);

	set_vesacea_prio_all();
	get_best_videoformat(&cea, &vesaceanr);

	/* Check if fb is created */
	/* Get fb dev name */
	disponoff = dispdevice_file_open(DISPONOFF_FILE, O_RDWR);
	if (disponoff < 0) {
		LOGHDMILIB("***** Failed to open %s *****", DISPONOFF_FILE);
		ret = -3;
		goto hdmiplugged_handle_end;
	}
	read_res = read(disponoff, buf, sizeof(buf));
	if (read_res > 0) {
		LOGHDMILIB("fbname:%s", buf);
	} else {
		/* Create frame buffer with best resolution */
		lseek(disponoff, 0, SEEK_SET);
		sprintf(req_str, "%02x%02x%02x", 1, cea, vesaceanr);
		LOGHDMILIB("req_str:%s", req_str);

		wr_res = write(disponoff, req_str, strlen(req_str));
		if (wr_res != (int)strlen(req_str)) {
			LOGHDMILIB("***** Failed to write %s *****",
					DISPONOFF_FILE);
			close(disponoff);
			ret = -4;
			goto hdmiplugged_handle_end;
		}

		/* Check that fb was created */
		/* Get fb dev name */
		lseek(disponoff, 0, SEEK_SET);
		read_res = read(disponoff, buf, sizeof(buf));
		if (read_res <= 0) {
			LOGHDMILIB("***** Failed to read %s *****",
						DISPONOFF_FILE);
			close(disponoff);
			ret = -5;
			goto hdmiplugged_handle_end;
		}

		LOGHDMILIB("fbname:%s", buf);
	}
	close(disponoff);

	/* Change resolution to be sure to have correct freq */
	hdmi_fb_chres(cea, vesaceanr);

hdmiplugged_handle_end:
	LOGHDMILIB("%s end:%d", __func__, ret);
	return ret;
}

static int hdmiunplugged_handle(void)
{
	enum hdmi_power_state power_state;

	LOGHDMILIB("%s", "HDMIEVENT_HDMIUNPLUGGED");
	plugstate_set(HDMI_UNPLUGGED);

	/* Allow early suspend */
	stayalive(0);
	return 0;
}

static int hdmiunknownhandle(int event)
{
	LOGHDMILIB("HDMIEVENT_EVENTUNKNOWN: %d", event);
	return 0;
}

/* Close frame buffer */
static int hdmi_fb_close(void)
{
	int disponoff;
	char req_str[7];
	int wr_res;

	LOGHDMILIB("%s begin", __func__);
	LOGHDMILIB("hdmi_fb_state:%d", hdmi_fb_state);

	if (hdmi_fb_state == HDMI_FB_CLOSED) {
		LOGHDMILIB("%s", "FB already closed");
		return 0;
	}

	/* Destroy frame buffer */
	disponoff = dispdevice_file_open(DISPONOFF_FILE, O_WRONLY);
	if (disponoff < 0) {
		LOGHDMILIB("***** Failed to open %s *****", DISPONOFF_FILE);
	} else {
		sprintf(req_str, "%02x%02x%02x", 0, 0, 0);
		LOGHDMILIB("req_str:%s", req_str);

		wr_res = write(disponoff, req_str, strlen(req_str));
		close(disponoff);
		if (wr_res != (int)strlen(req_str))
			LOGHDMILIB("***** Failed to write %s *****",
						DISPONOFF_FILE);
	}

	hdmievclr(EVENTMASK_ALL);

	hdmi_fb_state = HDMI_FB_CLOSED;
	LOGHDMILIB("%s end", __func__);
	return 0;
}

/* Send Infoframe */
static int infofr_send(__u8 type, __u8 ver, __u8 crc, __u8 len, __u8 *data)
{
	int infofrfd;
	char buf[128];
	int res = 0;

	LOGHDMILIB("%s begin", __func__);

	buf[0] = type;
	buf[1] = ver;
	buf[2] = crc;
	buf[3] = len;
	memcpy(&buf[4], data, len);

	infofrfd = open(INFOFRSEND_FILE, O_WRONLY);
	if (infofrfd <= 0) {
		LOGHDMILIB("***** Failed to open %s *****\n", INFOFRSEND_FILE);
		res = -1;
		goto infofr_send_end;
	}

	res = write(infofrfd, buf, len + 4);
	if (res != len + 4) {
		LOGHDMILIB("***** infofrsend failed %d *****\n", res);
		res = -1;
		goto infofr_send_end;
	}

	if (infofrfd > 0)
		close(infofrfd);

infofr_send_end:
	LOGHDMILIB("%s end:%d", __func__, res);
	return res;
}

/* Send plug event message on client socket */
static int plugevent_send(__u32 cmd, int audio_support, int nr,
					struct vesacea vesacea[])
{
	int res = 0;
	int val;
	__u8 buf[128];
	int sock;
	__u32 cmd_id;
	int cnt;

	LOGHDMILIB("%s begin", __func__);

	LOGHDMILIB("audio_support:%d", audio_support);
	LOGHDMILIB("nr video supp:%d", nr);

	cmd_id = get_new_cmd_id_ind();

	val = cmd;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	memcpy(&buf[CMDID_OFFSET], &cmd_id, 4);
	val = 2 + nr * 2;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	buf[CMDLEN_OFFSET] = audio_support;
	buf[CMDLEN_OFFSET + 1] = nr;
	for (cnt = 0; cnt < nr; cnt++) {
		buf[CMDLEN_OFFSET + 2 + cnt * 2] = vesacea[cnt].cea;
		buf[CMDLEN_OFFSET + 3 + cnt * 2] = vesacea[cnt].nr;
	}

	/* Send on socket */
	res = clientsocket_send(buf, CMDBUF_OFFSET + val);
	LOGHDMILIB("%s end", __func__);
	return res;
}

/* Add command to list */
int cmd_add(struct cmd_data *cmd)
{
	struct cmd_data **cmd_obj;
	struct cmd_data *cmd_new;

	LOGHDMILIB("%s begin", __func__);

	cmd_new = malloc(sizeof(struct cmd_data));
	if (cmd_new)
		memcpy(cmd_new, cmd, sizeof(struct cmd_data));
	else
		return -1;

	/* Add to list */
	pthread_mutex_lock(&cmd_mutex);
	cmd_obj = &cmd_data;
	while (*cmd_obj != NULL)
		cmd_obj = &((*cmd_obj)->next);
	*cmd_obj = cmd_new;
	pthread_mutex_unlock(&cmd_mutex);

	LOGHDMILIB("%s end", __func__);
	return 0;
}

/* Delete command list */
static int cmd_del_all(void)
{
	struct cmd_data **cmd_obj;
	struct cmd_data *cmd_del;

	LOGHDMILIB("%s begin", __func__);
	pthread_mutex_lock(&cmd_mutex);
	cmd_obj = &cmd_data;
	while (*cmd_obj != NULL) {
		cmd_del = *cmd_obj;
		cmd_obj = &((*cmd_obj)->next);
		free(cmd_del);
	}
	pthread_mutex_unlock(&cmd_mutex);
	LOGHDMILIB("%s end", __func__);
	return 0;
}

/* Signal an event to main thread */
int hdmi_event(int event)
{
	pthread_mutex_lock(&event_mutex);
	hdmi_events |= event;
	if (hdmi_events)
		pthread_cond_signal(&event_cond);
	pthread_mutex_unlock(&event_mutex);
	return 0;
}

/* Handling of received command */
static int hdmi_eventcmd(void)
{
	struct cmd_data *cmd_obj = NULL;
	struct cmd_data *del_obj;
	int res = 0;
	int ret = -1;
	enum hdmi_power_state power_state;
	enum hdmi_plug_state plug_state;
	int handlecmd;

	LOGHDMILIB("%s begin", __func__);

	pthread_mutex_lock(&cmd_mutex);
	if (cmd_data) {
		cmd_obj = cmd_data;
		cmd_data = cmd_data->next;
	}
	pthread_mutex_unlock(&cmd_mutex);

	/* Handle all mesages in list */
	while (cmd_obj) {
		res = -1;
		handlecmd = 1;
		ret = cmd_obj->cmd;

		/* Check power and plug state */
		switch (cmd_obj->cmd) {
		case HDMI_ENABLE:
		case HDMI_DISABLE:
		case HDMI_EXIT:
		case HDMI_CECSEND:
		default:
			break;

		case HDMI_EDIDREQ:
		case HDMI_FB_RES_SET:
		case HDMI_HDCP_INIT:
		case HDMI_INFOFR:
			handlecmd = 0;
			powerstate_get(&power_state);
			plugstate_get(&plug_state);
			if (power_state != HDMI_POWERON)
				illegalstate_send(HDMI_ILLSTATE_UNPOWERED,
							cmd_obj->cmd_id);
			else if (plug_state != HDMI_PLUGGED)
				illegalstate_send(HDMI_ILLSTATE_UNPLUGGED,
							cmd_obj->cmd_id);
			else
				handlecmd = 1;
			break;

		case HDMI_FB_RELEASE:
			handlecmd = 0;
			powerstate_get(&power_state);
			plugstate_get(&plug_state);
			if ((power_state == HDMI_POWERON) &&
						(plug_state == HDMI_PLUGGED))
				illegalstate_send(HDMI_ILLSTATE_PWRON_PLUGGED,
							cmd_obj->cmd_id);
			else
				handlecmd = 1;
			break;
		}

		if (handlecmd == 0)
			break;

		/* Handle cmd */
		switch (cmd_obj->cmd) {
		case HDMI_ENABLE:
			/* Subscribe on plug events */
			hdmiplug_subscribe();

			/* Subscribe for cec events */
			res = cecrx_subscribe();
			break;

		case HDMI_DISABLE:
			res = hdmi_fb_close();
			break;

		case HDMI_EDIDREQ:
			res = edidreq(cmd_obj->data[0], cmd_obj->cmd_id);
			break;

		case HDMI_CECSEND:
			res = cecsend(cmd_obj->cmd_id,
					cmd_obj->data[0],
					cmd_obj->data[1],
					cmd_obj->data[2],
					&cmd_obj->data[3]);
			break;

		case HDMI_FB_RES_SET:
			res = hdmi_fb_chres(cmd_obj->data[0], cmd_obj->data[1]);
			break;

		case HDMI_FB_RELEASE:
			hdmi_fb_close();

			/* Subscribe on plug events */
			hdmiplug_subscribe();

			/* Subscribe for cec events */
			res = cecrx_subscribe();
			break;

		case HDMI_HDCP_INIT:
			if (cmd_obj->data_len !=
					(AES_KEYS_SIZE + CMDBUF_OFFSET))
				res = -1;
			else
				res = hdcp_init(cmd_obj->data);
			break;

		case HDMI_VESACEAPRIO_SET:
			res = vesaceaprio_set(cmd_obj->data[0],
						&cmd_obj->data[1]);
			break;

		case HDMI_INFOFR:
			res = infofr_send(cmd_obj->data[0],
						cmd_obj->data[1],
						cmd_obj->data[2],
						cmd_obj->data[3],
						&cmd_obj->data[4]);
			break;

		case HDMI_EXIT:
			hdmi_fb_close();
			res = 0;

			/* delete list */
			cmd_del_all();

			hdmievwakeupfile_wr();

			goto hdmi_eventcmd_end;
			break;

		default:
			break;
		}

		LOGHDMILIB("cmd:%d cmd_id:%x res:%d\n", cmd_obj->cmd,
							cmd_obj->cmd_id, res);

		free(cmd_obj);

		pthread_mutex_lock(&cmd_mutex);
		if (cmd_data) {
			cmd_obj = cmd_data;
			cmd_data = cmd_data->next;
		} else
			cmd_obj = NULL;
		pthread_mutex_unlock(&cmd_mutex);
	}

hdmi_eventcmd_end:
	LOGHDMILIB("%s end", __func__);

	if (res != 0)
		ret = res;

	return ret;
}

static int hdmi_service_exit_do(void)
{
	int sock;
	int res;

	LOGHDMILIB("%s begin", __func__);

	/* Shutdown listen socket to end listen thread */
	sock = listensocket_get();
	listensocket_set(-1);
	res = shutdown(sock, SHUT_RDWR);

	pthread_mutex_destroy(&event_mutex);
	pthread_mutex_destroy(&cmd_mutex);
	pthread_mutex_destroy(&fb_state_mutex);
	pthread_cond_destroy(&event_cond);

	LOGHDMILIB("%s end", __func__);
	return res;
}

/* Main thread. Handles messages from client thread or kernel event thread */
static void thread_main_fn(void *arg)
{
	int events;
	int cont = 1;
	int dummy = 0;
	int res;
	int audio_support;
	int nr_video;
	struct vesacea video_supported[FORMATS_MAX];

	LOGHDMILIB("%s begin", __func__);

	pthread_create(&thread_kevent, NULL, (void *)thread_kevent_fn,
			(void *)&dummy);

	pthread_create(&thread_socklisten, NULL, (void *)thread_socklisten_fn,
			(void *)&dummy);

	while (cont) {
		/* Wait for event */
		pthread_mutex_lock(&event_mutex);
		if (hdmi_events == 0)
			/* Wait only if there are no events pending.
			 * event_mutex is automatically unlocked while waiting
			 * and locked again when thread is awakened.
			 */
			pthread_cond_wait(&event_cond, &event_mutex);
		events = hdmi_events;
		hdmi_events = 0;
		pthread_mutex_unlock(&event_mutex);

		LOGHDMILIB("%s: event:%x", __func__, events);

		/* kernel events */
		if (events & HDMIEVENT_HDMIPLUGGED) {
			if (hdmiplugged_handle(&audio_support) == 0) {
				vesacea_supported(&nr_video, video_supported);
				plugevent_send(HDMI_PLUGGED_EV, audio_support,
						nr_video,
						video_supported);
			}
		} else if (events & HDMIEVENT_HDMIUNPLUGGED) {
			hdmiunplugged_handle();
			plugevent_send(HDMI_UNPLUGGED_EV, 0, 0, NULL);
		}

		if (events & HDMIEVENT_CEC)
			cecrx();
		if (events & HDMIEVENT_HDCP)
			hdcp_state();
		if (events & HDMIEVENT_CECTXERR)
			cecsenderr();

		/* App cmd event */
		if (events & HDMIEVENT_CMD) {
			res = hdmi_eventcmd();
			if (res == HDMI_EXIT) {
				cont = 0;
				/* Wait for kevent thread to exit */
				usleep(2000000);
			}
		}
	}

	pthread_mutex_lock(&event_mutex);
	hdmi_events = 0;
	pthread_mutex_unlock(&event_mutex);

	hdmi_service_exit_do();

	LOGHDMILIB("%s end", __func__);

	/* Exit thread */
	pthread_exit(NULL);
}

/* API helper functions */
int hdmi_service_init(int avoid_return_msg)
{
	int dummy = 0;
	int socket;

	LOGHDMILIB("%s begin", __func__);

#ifdef HDMI_SERVICE_USE_CALLBACK_FN
	hdmi_service_callback_set(NULL);
#endif /*HDMI_SERVICE_USE_CALLBACK_FN*/

	/* Set sysfs format to binary */
	storeastext(0);

	vesacea_prio_default();

	pthread_mutex_init(&event_mutex, NULL);
	pthread_mutex_init(&cmd_mutex, NULL);
	pthread_mutex_init(&fb_state_mutex, NULL);
	pthread_cond_init(&event_cond, NULL);

	/* Create threads */
	pthread_create(&thread_main, NULL, (void *)thread_main_fn,
			(void *)&dummy);

	LOGHDMILIB("%s end", __func__);

	/* Wait for threads to start */
	usleep(100000);
	socket = serversocket_create(avoid_return_msg);

	return socket;
}

int hdmi_service_exit(void)
{
	int val;
	__u8 buf[32];
	val = HDMI_EXIT;

	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = 0;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}

int hdmi_service_enable(void)
{
	int val;
	__u8 buf[32];
	val = HDMI_ENABLE;

	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = 0;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}

int hdmi_service_disable(void)
{
	int val;
	__u8 buf[32];
	val = HDMI_DISABLE;

	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = 0;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}

#ifdef HDMI_SERVICE_USE_CALLBACK_FN
void hdmi_service_callback_set(cb_fn hdmi_cb)
{
	hdmi_callback_fn = hdmi_cb;
}

cb_fn hdmi_service_callback_get(void)
{
	return hdmi_callback_fn;
}
#endif /*HDMI_SERVICE_USE_CALLBACK_FN*/

int hdmi_service_resolution_set(int cea, int vesaceanr)
{
	int val;
	__u8 buf[32];

	val = HDMI_FB_RES_SET;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = 2;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	/* data */
	buf[CMDBUF_OFFSET] = cea;
	buf[CMDBUF_OFFSET + 1] = vesaceanr;
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}

int hdmi_service_fb_release(void)
{
	int val;
	__u8 buf[32];

	val = HDMI_FB_RELEASE;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = 0;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}

int hdmi_service_cec_send(__u8 initiator, __u8 destination, __u8 data_size,
							__u8 *data)
{
	int val;
	__u8 buf[32];

	if (data_size > CEC_MSG_SIZE_MAX)
		return -1;

	val = HDMI_CECSEND;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = data_size + 3;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	/* data */
	buf[CMDBUF_OFFSET] = initiator;
	buf[CMDBUF_OFFSET + 1] = destination;
	buf[CMDBUF_OFFSET + 2] = data_size;
	memcpy(&buf[CMDBUF_OFFSET + 3], data, data_size);
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}

int hdmi_service_edid_request(__u8 block)
{
	int val;
	__u8 buf[32];

	if (block >= 2)
		return -1;

	val = HDMI_EDIDREQ;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = 1;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	/* data */
	buf[CMDBUF_OFFSET] = block;
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}

int hdmi_service_hdcp_init(__u16 aes_size, __u8 *aes_data)
{
	int val;
	__u8 buf[300];

	if (aes_size != AES_KEYS_SIZE)
		return -1;

	val = HDMI_HDCP_INIT;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = aes_size;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	/* data */
	memcpy(&buf[CMDBUF_OFFSET], aes_data, aes_size);
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}

int hdmi_service_infoframe_send(__u8 type, __u8 version, __u8 crc,
						__u8 data_size, __u8 *data)
{
	int val;
	__u8 buf[300];

	if (data_size > INFOFR_MSG_SIZE_MAX)
		return -1;

	val = HDMI_INFOFR;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = data_size + 4;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	/* data */
	buf[CMDBUF_OFFSET] = type;
	buf[CMDBUF_OFFSET + 1] = version;
	buf[CMDBUF_OFFSET + 2] = crc;
	buf[CMDBUF_OFFSET + 3] = data_size;
	memcpy(&buf[CMDBUF_OFFSET + 4], data, data_size);
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}

int hdmi_service_vesa_cea_prio_set(__u8 vesa_cea1, __u8 nr1,
				__u8 vesa_cea2, __u8 nr2,
				__u8 vesa_cea3, __u8 nr3)
{
	int val;
	__u8 buf[32];

	val = HDMI_VESACEAPRIO_SET;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	/* cmd_id */
	val = 0;
	memcpy(&buf[CMDID_OFFSET], &val, 4);
	/* len */
	val = 7;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	/* data */
	buf[CMDBUF_OFFSET] = 3;
	buf[CMDBUF_OFFSET + 1] = vesa_cea1;
	buf[CMDBUF_OFFSET + 2] = nr1;
	buf[CMDBUF_OFFSET + 3] = vesa_cea2;
	buf[CMDBUF_OFFSET + 4] = nr2;
	buf[CMDBUF_OFFSET + 5] = vesa_cea3;
	buf[CMDBUF_OFFSET + 6] = nr3;
	serversocket_write(CMDBUF_OFFSET + val, buf);

	return 0;
}
