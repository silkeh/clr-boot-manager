/*
 * This file is based on mbr.bin and gptmbr.bin from SYSLINUX.
 *
 * SYSLINUX is:
 *
 *  Copyright 1994-2011 H. Peter Anvin et al - All Rights Reserved
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *  Boston MA 02111-1307, USA; either version 2 of the License, or
 *  (at your option) any later version; incorporated herein by reference.
 *
 */

#define MBR_BIN_LEN 440

extern unsigned char syslinux_mbr_bin[];
extern unsigned char syslinux_gptmbr_bin[];
