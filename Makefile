# Makefile for HDMIservice
#
# Copyright (C) ST-Ericsson AB 2011.
#

PACKAGE_NAME=hdmi_service

CFLAGS += -c -Wall -O2 -fPIC
# no-as-needed is a work-around, pthread is dropped from the DT_NEEDED
# for some reason, and yet the final link fails due to missing symbols
LDFLAGS += -L./ -Wl,--no-as-needed -lpthread -shared
LDFLAGS_2 = -L./
INCLUDES += -I./include
HDMILIBS = hdmiservice.so

build: hdmiservice.so hdmistart

install: build
#	@$(PACKAGE_FILE) /usr/lib/hdmiservice.so $(CURDIR)/hdmiservice.so 755 0 0
#	@$(PACKAGE_FILE) /usr/bin/hdmistart $(CURDIR)/hdmistart 755 0 0
	mkdir -p $(DESTDIR)/usr/lib
	mkdir -p $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/usr/include
	cp $(CURDIR)/hdmiservice.so $(DESTDIR)/usr/lib
	cp $(CURDIR)/hdmistart $(DESTDIR)/usr/bin
	cp $(CURDIR)/include/*.h $(DESTDIR)/usr/include

%.o: src/%.c
	${CC} ${CFLAGS} ${INCLUDES} -c $<

hdmiservice.so: cec.o edid.o hdcp.o hdmi_service_api.o hdmi_service.o kevent.o \
	setres.o socket.o
	$(CC) $(LDFLAGS) $^ -o $@

hdmistart: hdmi_service_start.o $(HDMILIBS)
	$(CC) $(LDFLAGS_2) $^ -o $@ $(HDMILIBS)

clean:
	@rm -rf cec.o edid.o hdcp.o hdmi_service_api.o hdmi_service.o kevent.o \
	setres.o socket.o hdmiservice.so hdmi_service_start.o hdmistart

.PHONY: hdmiservice.so clean
