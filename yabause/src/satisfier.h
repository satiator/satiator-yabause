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

/*! \file satisfier.h
    \brief Saturn Satisfier emulation bits.
*/

#ifndef _SATISFIER_H
#define _SATISFIER_H

#include <stdint.h>

// FatFS config and prototypes
// avoid name collision between dirent.h and ff.h
#include "satisfier/ff.h"

typedef enum {
    c_get_status = 0x90,
    c_write_buffer,
    c_read_buffer,

    c_mkfs = 0x94,
    c_fsinfo,
    c_settime,

    c_open = 0xA0,
    c_close,
    c_seek,
    c_read,
    c_write,
    c_truncate,
    c_stat,
    c_rename,
    c_unlink,
    c_mkdir,
    c_opendir,
    c_readdir,
    c_chdir,
    c_emulate,
} satisfier_cmd_t;

#define C_SEEK_SET  0
#define C_SEEK_CUR  1
#define C_SEEK_END  2

// CD image descriptor
typedef struct {
    uint8_t number;              // track number, 1-99. 100, 101, 102 correspond to 0xa0, 0xa1, 0xa2 entries.
    uint32_t toc_ent;           // TOC entry as read by Saturn. FAD, addr, ctrl.
    uint32_t file_offset;       // byte offset in track file of start of track data
    uint8_t file_secsize;       // sector size (enum)
    uint8_t namelen;           // length of following name (no terminating null)
    char name[];
} __attribute__((packed)) satisfier_trackdesc_t;

#define SEC_2048    0
#define SEC_2352    1
#define SEC_2448    2

#endif // _SATISFIER_H
