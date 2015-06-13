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

/*! \file satisfier.c
  \brief Saturn Satisfier command and status emulation.
  */

#define CDB_HIRQ_CMOK      0x0001
#define CDB_HIRQ_DRDY      0x0002
#define CDB_HIRQ_CSCT      0x0004
#define CDB_HIRQ_BFUL      0x0008
#define CDB_HIRQ_PEND      0x0010
#define CDB_HIRQ_DCHG      0x0020
#define CDB_HIRQ_ESEL      0x0040
#define CDB_HIRQ_EHST      0x0080
#define CDB_HIRQ_ECPY      0x0100
#define CDB_HIRQ_EFLS      0x0200
#define CDB_HIRQ_SCDQ      0x0400
#define CDB_HIRQ_MPED      0x0800
#define CDB_HIRQ_MPCM      0x1000
#define CDB_HIRQ_MPST      0x2000


#include "cs2.h"
#include "satisfier.h"
#include "debug.h"

// Glue to allow using the raw Satisfier source code for best compatibility
#define write_bios_sector() // not implemented
#define finish_cmd()
#define read_buf(n)
#define write_buf(n)
#define setup_cmd()   do {  \
    f = get_file();         \
    handle = get_handle();  \
    length = get_length();  \
    cmd = cdb_buf[0] >> 8;  \
} while(0)

int SatisfierCDOpenDescriptor(const char *name);
static satisfier_struct *satisfier = NULL;

void start_emulation(const char *filename) {
    if (SatisfierCDOpenDescriptor(filename)) {
        SATISLOG("Failed to open CD descriptor, aborting drive emulation attempt\n");
        return;
    }

    // Command interface is disabled on successful emulation start
    satisfier->active = 0;
}

#include "satisfier/cdb_command_impl.c"

int SatisfierExecute(Cs2 * Cs2Area, u16 instruction) {
    u16 len;
    satisfier_struct *sat = Cs2Area->satisfier;
    if (!sat || !sat->active)
        return -1;

    switch (instruction) {
        // I/O glue commands
        case c_get_status:
            Cs2Area->reg.CR1 = cdb_buf[0];
            Cs2Area->reg.CR2 = cdb_buf[1];
            Cs2Area->reg.CR3 = cdb_buf[2];
            Cs2Area->reg.CR4 = cdb_buf[3];
            Cs2Area->reg.HIRQ |= CDB_HIRQ_CMOK;
            SATISLOG("read status\n");
            return 0;

        case c_read_buffer:
        case c_write_buffer:
            // TODO: check DMA direction, also cancel pending normal DMA
            len = Cs2Area->reg.CR4;
            if (len > 2048) {
                Cs2Area->reg.CR1 = Cs2Area->reg.CR2 = Cs2Area->reg.CR3 = Cs2Area->reg.CR4 = 0xffff;
            } else {
                sat->dma_buf = (u16*)buffer;
                sat->dma_remain = (len+1)/2;
                Cs2Area->reg.HIRQ |= CDB_HIRQ_DRDY;
                Cs2Area->reg.CR1 = Cs2Area->reg.CR2 = Cs2Area->reg.CR3 = Cs2Area->reg.CR4 = 0;
            }
            Cs2Area->reg.HIRQ |= CDB_HIRQ_CMOK | CDB_HIRQ_EHST;
            SATISLOG("started DMA\n");
            return 0;

            // core commands are all emulated using the Satisfier code
        case c_mkfs:
        case c_fsinfo:
        case c_settime:
        case c_open:
        case c_close:
        case c_seek:
        case c_read:
        case c_write:
        case c_truncate:
        case c_stat:
        case c_rename:
        case c_unlink:
        case c_mkdir:
        case c_opendir:
        case c_readdir:
        case c_chdir:
        case c_emulate:
            cdb_buf[0] = Cs2Area->reg.CR1;
            cdb_buf[1] = Cs2Area->reg.CR2;
            cdb_buf[2] = Cs2Area->reg.CR3;
            cdb_buf[3] = Cs2Area->reg.CR4;
            SATISLOG("cmd: %04X %04X %04X %04X ", cdb_buf[0], cdb_buf[1], cdb_buf[2], cdb_buf[3]);

            cdb_command();
            // TODO: MPED is set by completing commands, in practice after some delay for file I/O
            Cs2Area->reg.HIRQ |= CDB_HIRQ_CMOK | CDB_HIRQ_MPED;
            Cs2Area->reg.CR1 = Cs2Area->reg.CR2 = Cs2Area->reg.CR3 = Cs2Area->reg.CR4 = 0;
            SATISLOG("resp: %04X %04X %04X %04X\n", cdb_buf[0], cdb_buf[1], cdb_buf[2], cdb_buf[3]);
            return 0;

        default: // not a Satisfier command
            return -1;
    }
}

//////////////////////////////////////////////////////////////////////////////

int SatisfierInit(Cs2 *Cs2Area, const char *basedir) {
    // TODO: config options
    satisfier_struct * sat = malloc(sizeof(satisfier_struct));
    if (!sat)
        return -1;

    memset(sat, 0, sizeof(*sat));

    Cs2Area->satisfier = sat;
    satisfier = sat;
    sat->basedir = basedir;
    sat->active = 1;  // XXX should be switchable
    if (f_mount(NULL, sat->basedir, 1) != FR_OK) {
        SATISLOG("failed to open directory '%s'\n", basedir);
        return -1;
    }

    SATISLOG("initialised in directory '%s'\n", basedir);
    return 0;
}
