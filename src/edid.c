/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Per Persson per.xb.persson@stericsson.com for
 * ST-Ericsson.
 *
 * License terms:
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <unistd.h>     /* Symbolic Constants */
#include <sys/types.h>  /* Primitive System Data Types */
#include <linux/types.h>
#include <errno.h>      /* Errors */
#include <stdarg.h>
#include <stdio.h>      /* Input/Output */
#include <stdlib.h>     /* General Utilities */
#include <string.h>     /* String handling */
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#ifdef ANDROID
#include <utils/Log.h>
#endif
#include "../include/hdmi_service_api.h"
#include "../include/hdmi_service_local.h"

struct edid_stdtim_ar {
	int x;
	int y;
};

const __u8 edidreqbl0[] = {0xA0, 0x00}; /* Request EDID block 0 */
const __u8 edidreqbl1[] = {0xA0, 0x01}; /* Request EDID block 1 */
const __u8 edid_block0_start[] = {
			0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
const __u8 edid_stdtim9_flag[] = {0x00, 0x00, 0x00, 0xFA, 0x00};
const __u8 edid_esttim3_flag[] = {0x00, 0x00, 0x00, 0xF7, 0x00};
const __u8 edid_esttim1_2_offset[] = {EDID_BL0_ESTTIM1_OFFSET,
					EDID_BL0_ESTTIM2_OFFSET};
const __u8 edid_esttim3_flag_offset[] = {EDID_BL1_ESTTIM3_1_FLAG_OFFSET,
					EDID_BL1_ESTTIM3_2_FLAG_OFFSET,
					EDID_BL1_ESTTIM3_3_FLAG_OFFSET};
const __u8 edid_stdtim9_flag_offset[] = {EDID_BL1_STDTIM9_1_FLAG_OFFSET,
					EDID_BL1_STDTIM9_2_FLAG_OFFSET,
					EDID_BL1_STDTIM9_3_FLAG_OFFSET};
/* Aspect ratios */
const struct edid_stdtim_ar edid_stdtim_ar[] = {
		{16, 10},
		{4, 3},
		{5, 4},
		{16, 9}
};

struct vesa_modes {
	int xres;
	int yres;
	int freq;
	int vesa_nr;
};

static struct vesa_modes vesa_modes[] = {
	{800, 600, 60, 9},
	{848, 480, 60, 14},
	{1024, 768, 60, 16},
	{1280, 768, 60, 23},
	{1280, 800, 60, 28},
	{1360, 768, 60, 39},
	{1366, 768, 60, 81}
};

static int get_vesanr_from_est_timing(int timing, int byte, int bit)
{
	int vesa_nr = -1;

	LOGHDMILIB2("timing:%d bit:%d", timing, bit);

	switch (timing) {
	/* Established Timing 1 */
	case 1:
		switch (bit) {
		case 5:
			vesa_nr = 4;
			break;
		case 0:
			vesa_nr = 9;
			break;
		default:
			break;
		}
		break;
	/* Established Timing 2 */
	case 2:
		switch (bit) {
		case 3:
			vesa_nr = 16;
			break;
		default:
			break;
		}
		break;
	/* Established Timing 3 */
	case 3:
		switch (byte) {
		case 6:
			switch (bit) {
			case 3:
				vesa_nr = 14;
				break;
			default:
				break;
			}
			break;
		case 7:
			switch (bit) {
			case 7:
				vesa_nr = 23;
				break;
			case 6:
				vesa_nr = 22;
				break;
			default:
				break;
			}
			break;
		case 8:
			switch (bit) {
			case 7:
				vesa_nr = 39;
				break;
			default:
				break;
			}
			break;
		}
		break;
	default:
		break;
	}

	return vesa_nr;
}

int get_vesanr_from_std_timing(int xres, int yres, int freq)
{
	int vesa_nr = -1;
	int nr_of_timings;
	int index;

	nr_of_timings = sizeof(vesa_modes)/sizeof(vesa_modes[0]);
	for (index = 0; index < nr_of_timings; index++) {
		if ((xres == vesa_modes[index].xres) &&
				(yres == vesa_modes[index].yres) &&
				(freq == vesa_modes[index].freq)) {
			vesa_nr = vesa_modes[index].vesa_nr;
			break;
		}
	}

	return vesa_nr;
}

/* Request and read EDID message for specified block */
int edid_read(__u8 block, __u8 *data)
{
	int edidread;
	int res;
	int result = 0;
	__u8 buf[16];
	int size;

	LOGHDMILIB("EDID read blk %d", block);
	/* Request edid block 0 */
	edidread = open(EDIDREAD_FILE, O_RDWR);
	if (edidread < 0) {
		LOGHDMILIB("***** Failed to open %s *****", EDIDREAD_FILE);
		result = -1;
		goto edid_read_end2;
	}

	if (block == 0) {
		size = sizeof(edidreqbl0);
		memcpy(buf, edidreqbl0, size);
	} else {
		size = sizeof(edidreqbl1);
		memcpy(buf, edidreqbl1, size);
	}

	res = write(edidread, buf, size);
	if (res < 0) {
		LOGHDMILIB("***** Failed to write %s *****", EDIDREAD_FILE);
		result = -2;
		goto edid_read_end1;
	}

	/* Check edid response */
	lseek(edidread, 0, SEEK_SET);
	res = read(edidread, data, EDIDREAD_SIZE);
	if (res != EDIDREAD_SIZE) {
		LOGHDMILIB("***** %s read error size: %d *****", EDIDREAD_FILE,
				res);
		result = -3;
		goto edid_read_end1;
	}

edid_read_end1:
	close(edidread);
edid_read_end2:
	return result;
}

/* Parse EDID block 0 */
int edid_parse0(__u8 *data, __u8 *extension, struct video_format formats[],
			int nr_formats)
{
	__u8 version;
	__u8 revision;
	__u8 est_timing;
	int vesa_nr;
	int bit;
	int cnt;
	int index;
	int xres;
	int yres;
	int byte;
	int ar_index;
	int freq;
	__u8 edidp;

	*extension = 0;

	/* Header */
	if (memcmp(data + EDID_BL0_HEADER_OFFSET, edid_block0_start, 8) != 0) {
		LOGHDMILIB("edid response:\n%02x %02x %02x %02x %02x %02x %02x "
				"%02x",
				*(data + 1),
				*(data + 2),
				*(data + 3),
				*(data + 4),
				*(data + 5),
				*(data + 6),
				*(data + 7),
				*(data + 8)
				);
		return EDIDREAD_FAIL;
	} else {
		LOGHDMILIB("%s", "--- EDID block 0 start OK ---");
	}

	/* Ver and Rev */
	version = *(data + EDID_BL0_VERSION_OFFSET);
	revision = *(data + EDID_BL0_REVISION_OFFSET);
	LOGHDMILIB("Ver:%d Rev:%d", version, revision);

	/* Read Established Timings 1&2 and set sink_support */
	for (index = 0; index <= 1; index++) {
		est_timing = *(data + edid_esttim1_2_offset[index]);
		LOGHDMILIB2("EstTim%d:%02x", index + 1, est_timing);
		if (est_timing == 0)
			continue;

		for (bit = 7; bit >= 0; bit--) {
			if (est_timing & (1 << bit)) {
				vesa_nr = get_vesanr_from_est_timing(index + 1,
									0, bit);
				LOGHDMILIB2("vesa_nr:%d", vesa_nr);
				if (vesa_nr < 1)
					continue;

				LOGHDMILIB2("EstTim1&2 try vesa_nr:%d",
							vesa_nr);
				for (cnt = 0; cnt < nr_formats; cnt++) {
					LOGHDMILIB3("with:%d",
							formats[cnt].vesaceanr);
					if ((formats[cnt].cea == 0) &&
						(formats[cnt].vesaceanr ==
								vesa_nr)) {
						formats[cnt].sink_support = 1;
						LOGHDMILIB("EstTim1&2 %d "
								"vesa_nr:%d",
								index + 1,
								vesa_nr);
						break;
					}
				}
			}
		}
	}

	/* Read Standard Timings 1-8 and set sink_support*/
	for (index = 0; index < EDID_BL0_STDTIM1_SIZE; index++) {
		edidp = EDID_BL0_STDTIM1_OFFSET + index * 2;
		xres = (*(data + edidp) + 31) * 8;
		byte = *(data + edidp + 1);
		ar_index = (byte & EDID_STDTIM_AR_MASK) >> EDID_STDTIM_AR_SHIFT;
		yres = xres * edid_stdtim_ar[ar_index].y /
					edid_stdtim_ar[ar_index].x;
		freq = 60 + ((byte & EDID_STDTIM_FREQ_MASK) >>
				EDID_STDTIM_FREQ_SHIFT);
		LOGHDMILIB2("xres:%d yres:%d freq:%d", xres, yres, freq);
		vesa_nr = get_vesanr_from_std_timing(xres, yres, freq);
		if (vesa_nr < 1)
			continue;

		LOGHDMILIB2("StdTim1to8 try vesa_nr:%d", vesa_nr);
		for (cnt = 0; cnt < nr_formats; cnt++) {
			LOGHDMILIB3("with:%d",
				formats[cnt].vesaceanr);
			if ((formats[cnt].cea == 0) &&
					(formats[cnt].vesaceanr ==
							vesa_nr)) {
				formats[cnt].sink_support = 1;
				LOGHDMILIB("StdTim1to8 %d vesa_nr:%d",
						index + 1,
						vesa_nr);
				break;
			}
		}
	}

	if (*(data + EDID_BL0_EXTFLAG_OFFSET) != 0)
		*extension = 1;

	return RESULT_OK;
}

/* Parse EDID block 1 */
int edid_parse1(__u8 *data, struct video_format formats[], int nr_formats,
		int *basic_audio_support, struct edid_latency *edid_latency,
		int *hdmi)
{
	__u8 tag;
	__u8 rev;
	__u8 offset;
	__u8 blockp;
	__u8 code;
	__u8 length = 0;
	__u8 ceanr;
	int index;
	int index2;
	int cnt;
	__u8 est_timing3;
	int byte;
	int bit;
	int vesa_nr;
	int xres;
	int yres;
	int ar_index;
	int freq;
	__u8 edidp;
	__u8 *p;

	tag = *(data + EDID_BL1_TAG_OFFSET);
	rev = *(data + EDID_BL1_REVNR_OFFSET);
	if (tag != EDID_BL1_TAG_EXPECTED) {
		LOGHDMILIB("edid bl1 tag:%02x or rev:%02x", tag, rev);
		return EDIDREAD_BL1_TAG_REV_ERR;
	}

	if (rev >= EDID_EXTVER_3)
		*hdmi = 1;
	else
		*hdmi = 0; /* Only DVI */

	offset = *(data + EDID_BL1_OFFSET_OFFSET);

	LOGHDMILIB("rev:%d offset:%d", rev, offset);

	/* Check Audio support */
	if (*(data + EDID_BL1_AUDIO_SUPPORT_OFFSET) &
			EDID_BASIC_AUDIO_SUPPORT_MASK) {
		*basic_audio_support = 1;
	}

	for (edidp = EDID_BLK_START; edidp < offset;
				edidp = edidp + length + 1) {
		code = (*(data + edidp) & EDID_BLK_CODE_MSK) >>
						EDID_BLK_CODE_SHIFT;
		length = *(data + edidp) & EDID_BLK_LENGTH_MSK;

		if ((offset + length) >= EDIDREAD_SIZE)
			return EDIDREAD_FAIL;

		LOGHDMILIB2("code:%d blklen:%d", code, length);

		switch (code) {
		case EDID_CODE_VIDEO:
			for (blockp = edidp + 1; blockp < edidp + 1 + length;
							blockp++) {
				ceanr = *(data + blockp) & EDID_SVD_ID_MASK;
				LOGHDMILIB2("try ceanr:%d", ceanr);
				for (cnt = 0; cnt < nr_formats; cnt++) {
					LOGHDMILIB3("with:%d",
							formats[cnt].vesaceanr);
					if ((formats[cnt].cea == 1) &&
						(formats[cnt].vesaceanr ==
								ceanr)) {
						formats[cnt].sink_support = 1;
						LOGHDMILIB("cea:%d", ceanr);
						break;
					}
				}
			}
			break;

		case EDID_CODE_VSDB:
			p = data + edidp;
			if (length >= (EDID_VSD_PHYS_SRC + 1)) {
				LOGHDMILIB("source physaddr:%02x%02x",
					*(p + EDID_VSD_PHYS_SRC),
					*(p + EDID_VSD_PHYS_SRC + 1));

				/*TODO logical addr (HDMI spec p.192)*/
			}

			/* Video and Audio latency */
			if ((length >= EDID_VSD_AUD_LAT) &&
				(*(p + EDID_VSD_LATENCY_IND) &
					EDID_VSD_LAT_FLD_MASK)) {
				edid_latency->video_latency =
				2 * (*(p + EDID_VSD_VID_LAT) - 1);
				edid_latency->audio_latency =
				2 * (*(p + EDID_VSD_AUD_LAT) - 1);
			}

			/* Interlaced Video and Audio latency */
			if ((length >= EDID_VSD_INTLCD_AUD_LAT) &&
				(*(p + EDID_VSD_LATENCY_IND) &
					EDID_VSD_INTLCD_LAT_FLD_MASK)) {
				edid_latency->intlcd_video_latency =
				2 * (*(p + EDID_VSD_INTLCD_VID_LAT) - 1);
				edid_latency->audio_latency =
				2 * (*(p + EDID_VSD_INTLCD_AUD_LAT) - 1);
			}
			break;

		default:
			break;
		}
	}

	/* Read Established Timing 3 and set sink_support */
	for (index = 0; index <= 2; index++) {
		edidp = edid_esttim3_flag_offset[index];

		/* Check for Established Timing3 flag */
		if (memcmp(data + edidp, edid_esttim3_flag,
				sizeof(edid_esttim3_flag)) != 0)
			/* Flag mismatch, this is not Established Timing 3 */
			continue;

		for (byte = EDID_BL1_ESTTIM3_BYTE_START;
				byte <= EDID_BL1_ESTTIM3_BYTE_END; byte++) {
			est_timing3 = *(data + edidp + byte);
			for (bit = 7; bit >= 0; bit--) {
				if ((est_timing3 & (1 << bit)) == 0)
					/* Not supported in sink */
					continue;

				vesa_nr = get_vesanr_from_est_timing(3, byte,
									bit);
				/* Set sink_suuport */
				LOGHDMILIB2("EstTim3 try vesa_nr:%d", vesa_nr);
				for (cnt = 0; cnt < nr_formats; cnt++) {
					LOGHDMILIB3("with:%d",
							formats[cnt].vesaceanr);
					if ((formats[cnt].cea == 0) &&
						(formats[cnt].vesaceanr ==
								vesa_nr)) {
						formats[cnt].sink_support = 1;
						LOGHDMILIB("EstTim3 vesa_nr:%d",
								vesa_nr);
						break;
					}
				}
			}
		}
	}

	/* Read Standard Timings 9-16 and set sink_support*/
	for (index2 = 0; index2 <= 2; index2++) {
		edidp = edid_stdtim9_flag_offset[index2];

		/* Check for Standard Timing flag */
		if (memcmp(data + edidp, edid_stdtim9_flag,
				sizeof(edid_stdtim9_flag)) != 0)
			/* Flag mismatch, this is not Standard Timing 9-16 */
			continue;

		for (index = 0; index < EDID_BL1_STDTIM9_SIZE; index++) {
			edidp += EDID_BL1_STDTIM9_BYTE_START + index * 2;
			xres = (*(data + edidp) + 31) * 8;
			byte = *(data + edidp + 1);
			ar_index = (byte & EDID_STDTIM_AR_MASK) >>
						EDID_STDTIM_AR_SHIFT;
			yres = xres * edid_stdtim_ar[ar_index].y /
						edid_stdtim_ar[ar_index].x;
			freq = 60 + ((byte & EDID_STDTIM_FREQ_MASK) >>
						EDID_STDTIM_FREQ_SHIFT);
			LOGHDMILIB2("xres:%d yres:%d freq:%d", xres, yres,
									freq);
			vesa_nr = get_vesanr_from_std_timing(xres, yres, freq);
			LOGHDMILIB2("StdTim9to16 try vesa_nr:%d", vesa_nr);
			for (cnt = 0; cnt < nr_formats; cnt++) {
				LOGHDMILIB3("with:%d",
					formats[cnt].vesaceanr);
				if ((formats[cnt].cea == 0) &&
						(formats[cnt].vesaceanr ==
								vesa_nr)) {
					formats[cnt].sink_support = 1;
					LOGHDMILIB("StdTim9to16 %d vesa_nr:%d",
							index + 1,
							vesa_nr);
					break;
				}
			}
		}
	}

	return RESULT_OK;
}

/* Get EDID message of specified block and send it on client socket */
int edidreq(__u8 block, __u32 cmd_id)
{
	int res = 0;
	int ret = 0;
	int edidsize = 0;
	int val;
	__u8 buf[512];
	__u8 ediddata[EDIDREAD_SIZE];

	LOGHDMILIB("%s begin", __func__);

	/* Request EDID */
	res = edid_read(block, ediddata);
	if (res == 0)
		edidsize = EDIDREAD_SIZE;

	val = HDMI_EDIDRESP;
	memcpy(&buf[CMD_OFFSET], &val, 4);
	memcpy(&buf[CMDID_OFFSET], &cmd_id, 4);
	val = edidsize + 1;
	memcpy(&buf[CMDLEN_OFFSET], &val, 4);
	buf[CMDBUF_OFFSET] = res;
	memcpy(&buf[CMDBUF_OFFSET + 1], ediddata, edidsize);

	/* Send on socket */
	ret = clientsocket_send(buf, CMDBUF_OFFSET + val);

	LOGHDMILIB("%s end", __func__);
	return ret;
}
