/*
 *  tslib/src/ts_test.c
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the GPL.  Please see the file
 * COPYING for more details.
 *
 * $Id: ts_test.c,v 1.1.1.1 2001/12/22 21:12:06 rmk Exp $
 *
 * Basic test program for touchscreen library.
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>

#include "tslib.h"

static int con_fd, fb_fd, last_vt = -1;
static struct fb_fix_screeninfo fix;
static struct fb_var_screeninfo var;
static struct fb_cmap cmap;
static char *fbuffer;

static int open_framebuffer(void)
{
	struct vt_stat vts;
	char vtname[16];
	int fd, nr;
	unsigned short col[2];

	fd = open("/dev/tty1", O_WRONLY);
	if (fd < 0) {
		perror("open /dev/tty1");
		return -1;
	}

	if (ioctl(fd, VT_OPENQRY, &nr) < 0) {
		perror("ioctl VT_OPENQRY");
		return -1;
	}
	close(fd);

	sprintf(vtname, "/dev/tty%d", nr);

	con_fd = open(vtname, O_RDWR | O_NDELAY);
	if (con_fd < 0) {
		perror("open tty");
		return -1;
	}

	if (ioctl(con_fd, VT_GETSTATE, &vts) == 0)
		last_vt = vts.v_active;

	if (ioctl(con_fd, VT_ACTIVATE, nr) < 0) {
		perror("VT_ACTIVATE");
		close(con_fd);
		return -1;
	}

	if (ioctl(con_fd, VT_WAITACTIVE, nr) < 0) {
		perror("VT_WAITACTIVE");
		close(con_fd);
		return -1;
	}

	if (ioctl(con_fd, KDSETMODE, KD_GRAPHICS) < 0) {
		perror("KDSETMODE");
		close(con_fd);
		return -1;
	}

	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd == -1) {
		perror("open /dev/fb");
		return -1;
	}

	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		perror("ioctl FBIOGET_FSCREENINFO");
		close(fb_fd);
		return -1;
	}

	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
		perror("ioctl FBIOGET_VSCREENINFO");
		close(fb_fd);
		return -1;
	}

	cmap.start = 0;
	cmap.len = 2;
	cmap.red = col;
	cmap.green = col;
	cmap.blue = col;
	cmap.transp = NULL;

	col[0] = 0;
	col[1] = 0xffff;

	if (ioctl(fb_fd, FBIOGETCMAP, &cmap) < 0) {
		perror("ioctl FBIOGETCMAP");
		close(fb_fd);
		return -1;
	}

	fbuffer = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, fb_fd, 0);
	if (fbuffer == (char *)-1) {
		perror("mmap framebuffer");
		close(fb_fd);
		return -1;
	}
	return 0;
}

static void close_framebuffer(void)
{
	munmap(fbuffer, fix.smem_len);
	close(fb_fd);

	if (ioctl(con_fd, KDSETMODE, KD_TEXT) < 0)
		perror("KDSETMODE");

	if (last_vt >= 0)
		if (ioctl(con_fd, VT_ACTIVATE, last_vt))
			perror("VT_ACTIVATE");

	close(con_fd);
}

static void sig(int sig)
{
	close_framebuffer();
	fflush(stderr);
	printf("signal %d caught\n", sig);
	fflush(stdout);
	exit(1);
}

static void put_cross(int x, int y, int c)
{
	int off, i, s, e;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= var.xres) x = var.xres - 1;
	if (y >= var.yres) y = var.yres - 1;

	off = y * fix.line_length + x * var.bits_per_pixel / 8;

	switch (var.bits_per_pixel) {
	case 16:
		s = y;
		if (s > 25)
			s = 25;

		e = var.yres - y;
		if (e > 25)
			e = 25;

		for (i = -s; i < e; i++) {
			fbuffer[off + i * fix.line_length + 0] = c;
			fbuffer[off + i * fix.line_length + 1] = c;
		}

		s = x;
		if (s > 25)
			s = 25;
		e = var.xres - x;
		if (e > 25)
			e = 25;

		for (i = -s; i < e; i++) {
			fbuffer[off + i * 2] = c;
			fbuffer[off + i * 2 + 1] = c;
		}
		break;

	case 8:
		break;
	}
}

int main()
{
	struct tsdev *ts;
	int x, y;

	signal(SIGSEGV, sig);
	signal(SIGINT, sig);
	signal(SIGTERM, sig);

	ts = ts_open("/dev/touchscreen/ucb1x00", 0);
	if (!ts) {
		perror("ts_open");
		exit(1);
	}

	if (ts_config(ts)) {
		perror("ts_config");
		exit(1);
	}

	if (open_framebuffer()) {
		close_framebuffer();
		exit(1);
	}

	memset(fbuffer, 0, fix.smem_len);

	x = var.xres / 2;
	y = var.yres / 2;

	put_cross(x, y, 0xff);

	while (1) {
		struct ts_sample samp;
		int ret;

		ret = ts_read(ts, &samp, 1);

		if (ret < 0) {
			perror("ts_read");
			close_framebuffer();
			exit(1);
		}

		if (ret != 1)
			continue;

		printf("%ld.%06ld: %6d %6d %6d\n", samp.tv.tv_sec, samp.tv.tv_usec,
			samp.x, samp.y, samp.pressure);

		if (samp.pressure > 100) {
			put_cross(x, y, 0);
			x = samp.x;
			y = samp.y;
			put_cross(x, y, 0xff);
		}
	}
}
