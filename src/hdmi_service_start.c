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
#include "../include/hdmi_service_api.h"

#define NO_RETURN_MESSAGES 1

int main(int argc, char *argv[])
{
	hdmi_init(NO_RETURN_MESSAGES);

	hdmi_enable();

	pthread_exit(NULL);

	return 0;
}
