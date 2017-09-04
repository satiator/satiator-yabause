
/*  Copyright 2015 James Laird-Wah

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "cdbase.h"
#include "debug.h"
#include "satisfier.h"
#include "satisfier/ff.h"
#include <stddef.h>
#include <stdlib.h>

static void seek_to_fad(u32 FAD);

static seg_desc_t *segs;
static uint16_t nsegs = 0;

static seg_desc_t cur_seg;

static int active = 0;
static FIL descfile;
static FIL trackfile;

static void add_leadin_leadout(int nsegs, seg_desc_t *segs) {
    // add leadin at start
    seg_desc_t *leadin = &segs[0];
    leadin->length = 150;
    leadin->track = 1;
    leadin->q_mode = 0x41;  // always a data track

    // add leadout at end
    seg_desc_t *leadout = &segs[nsegs-1];
    leadout->start = segs[nsegs-2].start + segs[nsegs-2].length;
    leadout->length = 0xffffffff;
    leadout->track = 102;
    leadout->q_mode = 0x01; // always audio
}

int SatisfierCDCloseDescriptor(void) {
    if (nsegs) {
        free(segs);
        nsegs = 0;
    }
    if (active) {
        f_close(&trackfile);
        active = 0;
    }
}

int SatisfierCDOpenDescriptor(const char *name) {
    int ret;
    SatisfierCDCloseDescriptor();

    SATISLOG("Opening desc %s\n", name);

    UINT nread;

    ret = f_open(&descfile, name, FA_READ);
    if (ret != FR_OK)
        return ret;

    ret = f_read(&descfile, &nsegs, 2, &nread);
    if (ret != FR_OK)
        return ret;

    nsegs += 2;
    segs = calloc(nsegs, sizeof(seg_desc_t));

    ret = f_read(&descfile, &segs[1], nsegs*sizeof(seg_desc_t), &nread);
    if (ret != FR_OK)
        return ret;

    add_leadin_leadout(nsegs, segs);

    for (int i=0; i<nsegs; i++)
        SATISLOG("seg %d %02x %x-%x\n", i, segs[i].q_mode, segs[i].start, segs[i].length);

    cur_seg = segs[0];

    return 0;
}

static int SatisfierCDInit(const char *);
static void SatisfierCDDeInit(void);
static s32 SatisfierCDReadTOC(u32 *);
static int SatisfierCDGetStatus(void);
static int SatisfierCDReadSectorFAD(u32, void *);
static void SatisfierCDReadAheadFAD(u32);

CDInterface SatisfierCD = {
	CDCORE_SATISFIER,
	"Saturn Satisfier Emulated CD Drive",
	SatisfierCDInit,
	SatisfierCDDeInit,
	SatisfierCDGetStatus,
	SatisfierCDReadTOC,
	SatisfierCDReadSectorFAD,
	SatisfierCDReadAheadFAD,
};

static int SatisfierCDInit(const char * cdrom_name) {
    // set up enough CD to trigger MPEG boot into the menu
    if (nsegs)
        free(segs);

    nsegs = 3;
    segs = calloc(3, sizeof(seg_desc_t));

    // just one data track
    seg_desc_t *seg = &segs[1];
    seg->start = 150;
    seg->length = 1000;
    seg->track = 1;
    seg->index = 1;
    seg->q_mode = 0x41;

    add_leadin_leadout(nsegs, segs);

	return 0;
}

static void SatisfierCDDeInit(void) {
}

static s32 SatisfierCDReadTOC(u32 * TOC)
{
    memset(TOC, 0xff, 102*4);

    int ntracks = 0;

    for (int i=0; i<nsegs; i++) {
        seg_desc_t *seg = &segs[i];
        if ((seg->index == 1) ||    // track start
            (seg->track == 102))    // leadout
            TOC[seg->track - 1] = (seg->q_mode << 24) | seg->start;

        if (seg->track < 100 && seg->track > ntracks)
            ntracks = seg->track;
    }

    TOC[99] = 0x41010000;   // first track = 1
    TOC[100] = 0x01000000 | (ntracks << 16);    // last track (audio assumed)

    return 102*4;
	// The format of TOC is as follows:
	// TOC[0] - TOC[98] are meant for tracks 1-99. Each entry has the
	// following format:
	// bits 0 - 23: track FAD address
	// bits 24 - 27: track addr
	// bits 28 - 31: track ctrl
	//
	// Any Unused tracks should be set to 0xFFFFFFFF
	//
	// TOC[99] - Point A0 information 
	// Uses the following format:
	// bits 0 - 7: PFRAME(should always be 0)
	// bits 7 - 15: PSEC(Program area format: 0x00 - CDDA or CDROM,
	//                   0x10 - CDI, 0x20 - CDROM-XA)
	// bits 16 - 23: PMIN(first track's number)
	// bits 24 - 27: first track's addr
	// bits 28 - 31: first track's ctrl
	//
	// TOC[100] - Point A1 information
	// Uses the following format:
	// bits 0 - 7: PFRAME(should always be 0)
	// bits 7 - 15: PSEC(should always be 0)
	// bits 16 - 23: PMIN(last track's number)
	// bits 24 - 27: last track's addr
	// bits 28 - 31: last track's ctrl
	//
	// TOC[101] - Point A2 information
	// Uses the following format:
	// bits 0 - 23: leadout FAD address
	// bits 24 - 27: leadout's addr
	// bits 28 - 31: leadout's ctrl
	//
	// Special Note: To convert from LBA/LSN to FAD, add 150.
}

static int open_count = 0;

static int SatisfierCDGetStatus(void) {
	// 0 - CD Present, disc spinning
	// 1 - CD Present, disc not spinning
	// 2 - CD not present
	// 3 - Tray open
    if (open_count) {
        open_count--;
        printf("open\n");
        return 3;
    }

    return 0;
}

void reader_desc_load(const char *filename) {
    printf("Load: %s\n", filename);
    if (SatisfierCDOpenDescriptor(filename)) {
        SATISLOG("Failed to open CD descriptor '%s', aborting drive emulation attempt\n", filename);
        return;
    }
    open_count = 1;    // pop the "lid" for a while
}


static void change_seg(uint32_t fad) {
    for (int i=0; i<nsegs; i++) {
        cur_seg = segs[i];
        SATISLOG("seg: %d-%d\n", cur_seg.start, cur_seg.length);
        if (fad >= cur_seg.start &&
            fad < cur_seg.start + cur_seg.length)
            break;
    }

    if (active) {
        f_close(&trackfile);
        active = 0;
    }

    if (cur_seg.filename_offset) {
        f_lseek(&descfile, cur_seg.filename_offset);
        uint8_t filename_len;
        UINT nread;
        f_read(&descfile, &filename_len, 1, &nread);
        char filename[filename_len + 1];
        f_read(&descfile, filename, filename_len, &nread);
        filename[filename_len] = '\0';

        int ret = f_open(&trackfile, filename, FA_READ);
        if (ret != FR_OK)
            printf("ERROR - couldn't open '%s'\n", filename);
        else
            active = 1;
    }
}

static const s8 syncHdr[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
static int SatisfierCDReadSectorFAD(u32 FAD, void *buffer) {
    memset(buffer, 0, 2352);
    SATISLOG("Read request %X\n", FAD);

    if ((FAD < cur_seg.start) ||
        (FAD >= (cur_seg.start + cur_seg.length)))
        change_seg(FAD);

    if (!active)    // ???
        return 1;
    f_lseek(&trackfile, cur_seg.file_offset + (FAD - cur_seg.start) * cur_seg.secsize);

    UINT nread;
    uint8_t *buf = buffer;
    if (cur_seg.secsize==2048) {
        // the Mode1 header needs to be set too XXX
        memcpy(buf, syncHdr, 12);
        buf += 16;
    }
    f_read(&trackfile, buf, cur_seg.secsize, &nread);

    SATISLOG("Read FAD %X - top %02X %02X %02X %02X\n", FAD, buf[0], buf[1], buf[2], buf[3]);

    return 1;
}

static void SatisfierCDReadAheadFAD(UNUSED u32 FAD)
{
	// No-op
}
