
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
#include <stddef.h>

// set externally by the CDB command
FIL cdb_descfile;

static FIL trackfile;
static int active = 0;
static struct {
    uint32_t toc_ent;
    uint32_t desc_offset;
} trackmap[102];
#define FAD_MASK 0xffffff

static int next_fad, track_start_fad, track_end_fad, sec_size;
static satisfier_trackdesc_t cur_track;

int SatisfierCDCloseDescriptor(void) {
    int i;
    if (!active)
        return;

    f_close(&trackfile);
    active = 0;
}

int SatisfierCDOpenDescriptor(const char *name) {
    int ret;
    SatisfierCDCloseDescriptor();
    memset(trackmap, 0xff, sizeof(trackmap));

    UINT nread;
    satisfier_trackdesc_t track;
    while (1) {
        ret = f_read(&cdb_descfile, &track, offsetof(satisfier_trackdesc_t, name), &nread);
        if (ret != FR_OK)
            return ret;
        if (nread < offsetof(satisfier_trackdesc_t, name))
            return -1;

        if (!track.number)  // last track marker
            break;
        if (track.number > 102)
            return -2;
        trackmap[track.number-1].toc_ent = track.toc_ent;
        trackmap[track.number-1].desc_offset = f_tell(&cdb_descfile) - offsetof(satisfier_trackdesc_t, name);
        f_lseek(&cdb_descfile, f_tell(&cdb_descfile) + track.namelen);

        SATISLOG("Track %d at FAD %X\n", track.number, track.toc_ent);
    }

    next_fad = -1;
    active = 1;

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
    memset(trackmap, 0xff, sizeof(trackmap));
    trackmap[  0].toc_ent = 0x41000096;
    trackmap[ 99].toc_ent = 0x41010000;
    trackmap[100].toc_ent = 0x41010000;
    trackmap[101].toc_ent = 0x0100ffff;
	return 0;
}

static void SatisfierCDDeInit(void) {
}

static s32 SatisfierCDReadTOC(u32 * TOC)
{
    int i;
    for (i=0; i<102; i++)
        TOC[i] = trackmap[i].toc_ent;
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

static int SatisfierCDGetStatus(void) {
	// 0 - CD Present, disc spinning
	// 1 - CD Present, disc not spinning
	// 2 - CD not present
	// 3 - Tray open

    return 0;
}

static void seek_to_fad(u32 FAD) {
    int ret;
    SATISLOG("Seek: %X\n", FAD);
    if (FAD >= (trackmap[101].toc_ent & FAD_MASK) ||    // leadout
        FAD < 150)                                      // leadin
        return;

    if (FAD < track_start_fad ||
        FAD >= track_end_fad) { // change track
        int track;
        for (track=0; track<100; track++) {
            if ((trackmap[track].toc_ent & FAD_MASK) <= FAD)
                break;
        }
        if (track == 100) {
            SATISLOG("Couldn't find a track with FAD 0x%X\n", FAD);
            return;
        }
        SATISLOG("Track desc off %X\n", trackmap[track].desc_offset);
        track_start_fad = trackmap[track].toc_ent & FAD_MASK;
        track_end_fad = trackmap[track+1].toc_ent & FAD_MASK;

        f_lseek(&cdb_descfile, trackmap[track].desc_offset);
        UINT nread;
        f_read(&cdb_descfile, &cur_track, offsetof(satisfier_trackdesc_t, name), &nread);
        switch (cur_track.file_secsize) {
            case SEC_2048:
                sec_size = 2048;
                break;
            case SEC_2352:
                sec_size = 2352;
                break;
            case SEC_2448:
                sec_size = 2448;
                break;
            default:
                SATISLOG("Bad sector size field 0x%02X in cdb_descfile\n", cur_track.file_secsize);
                return;
        }

        SATISLOG("Track %d secsz %d tocent %X namelen %d\n", cur_track.number, sec_size, cur_track.toc_ent, cur_track.namelen);

        char filename[257];
        f_read(&cdb_descfile, filename, cur_track.namelen, &nread);
        filename[cur_track.namelen] = 0;
        f_close(&trackfile);
        if (f_open(&trackfile, filename, FA_READ)) {
            SATISLOG("Couldn't open track file '%s'\n", filename);
            return;
        }

        SATISLOG("New track. FAD range %X-%X\n", track_start_fad, track_end_fad);
    }

    int target = (FAD - track_start_fad) * sec_size + cur_track.file_offset;
    ret = f_lseek(&trackfile, target);
    if (ret)
        SATISLOG("seek to %X failed: %X\n", target, ret);
    SATISLOG("sought to %X\n", f_tell(&trackfile));
    next_fad = FAD;
}

static const s8 syncHdr[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
static int SatisfierCDReadSectorFAD(u32 FAD, void *buffer) {
    memset(buffer, 0, 2352);
    if (!active)
        return 1;
    SATISLOG("Read request %X\n", FAD);
    
    if (FAD != next_fad) // seek (as it were)
        seek_to_fad(FAD);

    UINT nread;
    uint8_t *buf = buffer;
    if (sec_size==2048) {
        // the Mode1 header needs to be set too XXX
        memcpy(buf, syncHdr, 12);
        buf += 16;
    }
    f_read(&trackfile, buf, sec_size, &nread);
    next_fad++;

    SATISLOG("Read FAD %X - top %02X %02X %02X %02X\n", FAD, buf[0], buf[1], buf[2], buf[3]);

    return 1;
}

static void SatisfierCDReadAheadFAD(UNUSED u32 FAD)
{
	// No-op
}
