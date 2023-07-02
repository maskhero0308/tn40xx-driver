/*
 * read-marvell-fw - program to extract the firmware for X3310 and E2010 phys
 * from windows driver TN40xxmp_64.sys or TN40xxmp_32.sys
 * (c)2020 Hans-Frieder Vogt <hfvogt@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <asm/byteorder.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define FW_START4 0x10000000
#define FW_END 0xaaaaaaaa

u8 version[4];
u16 version2;
enum {
	X3310, E2010, UNKNOWN
} phy;

/*
 * PE32/PE32+ sys file seems to store data in be16 chunks
 */
u32 byteswap_data(u32 x) {
	u16 lowdata = __be16_to_cpu(0xffff & x);
	u16 highdata = __be16_to_cpu(x >> 16);

	return (u32)highdata << 16 | lowdata;
}

int write_output(int f, off_t oo, u32 r) {
	ssize_t l = 0;
	u8 d;

	if (oo == 0x120) {
		version[0] = (r >> 8) & 0xff;
		version[1] = r & 0xff;
		version[2] = (r >> 24) & 0xff;
		version[3] = (r >> 16) & 0xff;
	} else if (oo == 0x124) {
		version2 = (r & 0xff) << 8;
		version2 |= (r >> 8) & 0xff;
	} else if (oo == 0x138) {
		switch ((r >> 8) & 0xff) {
		case 1:
			phy = X3310;	
			break;
		case 3:
			phy = E2010;
			break;
		default:
			phy = UNKNOWN;
		}
	}

	d = (r >> 8) & 0xff;
	l += write(f, &d, 1);
	d = r & 0xff;
	l += write(f, &d, 1);
	d = (r >> 24) & 0xff;
	l += write(f, &d, 1);
	d = (r >> 16) & 0xff;
	l += write(f, &d, 1);
	if (l < 4) {
		fprintf(stderr, "write failed at file offset 0x%04x, err=\"%s\"\n",
			oo, strerror(errno));
		return -2;
	}
	return 0;
}

void usage(char *p) {
	printf("\nUsage:\n\t%s Win-driver-file.sys\n\n", p);
}

int main (int argc, char **argv) {
	int fd, fdout, i, ret = 0;
	char outname[20] = "marvell_fw_000.bin";
	char outnamenew[1024];
	int nout = 0;
	u32 r, r_old = 0, fw_len = 0;
	off_t o = 0, oo;
	off_t o_start = 0;
	int in_fw = 0;

	if (argc < 2) {
		fprintf(stderr, "%s: too few arguments\n", argv[0]);
		usage(argv[0]);
		return 1;
	}

	printf("Searching Marvell network phy firmware in windows driver file \"%s\"...\n", argv[1]);
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open of input file failed, err=\"%s\"\n",
			strerror(errno));
		return 1;
	}

	/* search for start of FW file */
	while (read(fd,&r,4) == 4) {
		if (!in_fw) {
			if (r == FW_START4) {
				fprintf(stderr, "found start sequence of FW at offset 0x%08x\n", o);
				sprintf(&outname[11], "%03d.bin", nout);
				o_start = o - 4;
				nout++;
				in_fw++;
				oo = 0;
				fdout = open(outname, O_CREAT | O_TRUNC | O_WRONLY,
					S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
				if (fdout < 0) {
					fprintf(stderr, "open of output file failed, err=\"%s\"\n",
						strerror(errno));
					return 1;
				}
				if (write_output(fdout, oo, r_old) < 0)
					return 2;
				oo += 4;
				if (write_output(fdout, oo, r) < 0)
					return 2;
				oo += 4;
			} else {
				r_old = r;
			}
		} else {
			if (write_output(fdout, oo, r) < 0)
				return 2;
			oo += 4;
			if (r == FW_END) {
				close(fdout);
				in_fw = 0;

				printf("version: %d.%d.%d.%d %04d %s\n",
					version[0], version[1], version[2], version[3],
					version2, phy == X3310 ? "x3310" : (phy == E2010 ? "e2010" : "unknw"));
				sprintf(outnamenew, "%sfw_%d_%d_%d_%d_%04d.hdr.new",
					phy == X3310 ? "x3310" : (phy == E2010 ? "e2010" : "unknw"),
					version[0], version[1], version[2], version[3],
					version2);
				fw_len = byteswap_data(r_old);
				if (fw_len + 32 == oo) {
					ret = rename(outname, outnamenew);
					if (ret < 0) {
						fprintf(stderr, "rename of output file failed, err=\"%s\"\n", strerror(errno));
					}
				} else {
					fprintf(stderr, "ERROR in fw file %s: length incorrect (should be: %d, is: %d\nDeleting file!\n", outname, fw_len + 32, oo);
					ret = unlink(outname);
					if (ret < 0) {
						fprintf(stderr, "unlink of output file failed, err=\"%s\"\n", strerror(errno));
					}
					lseek(fd, o_start + 8, SEEK_SET);
					o = o_start + 8;
					continue;
				}
			}
		}
		o += 4;
	}

	if (in_fw) {
		fprintf(stderr, "ERROR: premature end of input, incomplete firmware file \"%s\"\nDeleting it!\n",
			outname);
		ret = unlink(outname);
		if (ret < 0) {
			fprintf(stderr, "unlink of output file failed, err=\"%s\"\n", strerror(errno));
		}
	}

	return ret;
}
