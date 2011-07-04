/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Per Persson per.xb.persson@stericsson.com for
 * ST-Ericsson.
 * License terms: <FOSS license>.
 */

#include <unistd.h>     /* Symbolic Constants */
#include <sys/types.h>  /* Primitive System Data Types */
#include <linux/types.h>
#include <errno.h>      /* Errors */
#include <stdarg.h>
#include <stdio.h>      /* Input/Output */
#include <stdlib.h>     /* General Utilities */
#include <pthread.h>    /* POSIX Threads */
#include <string.h>     /* String handling */
#include <fcntl.h>
#include <time.h>
#include "../include/hdmi_service_api.h"
#include "../include/hdmi_service_local.h"

/* API functions
 * Input avoid_return_msg: set to 1 to avoid messages from service.
 * Return value: socket number where events will be notified.
 */
int hdmi_init(int avoid_return_msg)
{
	return hdmi_service_init(avoid_return_msg);
}

int hdmi_exit(void)
{
	return hdmi_service_exit();
}

int hdmi_enable(void)
{
	return hdmi_service_enable();
}

int hdmi_disable(void)
{
	return hdmi_service_disable();
}

#ifdef HDMI_SERVICE_USE_CALLBACK_FN
void hdmi_callback_set(void (*hdmi_cb)(int cmd, int data_size, __u8 *data))
{
	hdmi_service_callback_set(hdmi_cb);
}
#endif /*HDMI_SERVICE_USE_CALLBACK_FN*/

int hdmi_resolution_set(int cea, int vesaceanr)
{
	return hdmi_service_resolution_set(cea, vesaceanr);
}

int hdmi_fb_release(void)
{
	return hdmi_service_fb_release();
}

int hdmi_cec_send(__u8 initiator, __u8 destination, __u8 data_size, __u8 *data)
{
	return hdmi_service_cec_send(initiator, destination, data_size, data);
}

int hdmi_edid_request(__u8 block)
{
	return hdmi_service_edid_request(block);
}

int hdmi_hdcp_init(__u16 aes_size, __u8 *aes_data)
{
	return hdmi_service_hdcp_init(aes_size, aes_data);
}

int hdmi_infoframe_send(__u8 type, __u8 version, __u8 crc, __u8 data_size,
								__u8 *data)
{
	return hdmi_service_infoframe_send(type, version, crc, data_size, data);
}

int hdmi_vesa_cea_prio_set(__u8 vesa_cea1, __u8 nr1,
			__u8 vesa_cea2, __u8 nr2,
			__u8 vesa_cea3, __u8 nr3)
{
	return hdmi_service_vesa_cea_prio_set(vesa_cea1, nr1,
						vesa_cea2, nr2,
						vesa_cea3, nr3);
}
