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

// FatFS config and prototypes
// FatFS uses a type named DIR, which conflicts with dirent.h
#define DIR FF_DIR
#include "satisfier/ff.h"
#undef DIR

// emulated hardware state struct
typedef struct
{
   const char *basedir;
   FATFS fatfs;
   int active;
   int dma_remain;
   u16 *dma_buf;
} satisfier_struct;

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
} satisfier_cmd_t;

#endif // _SATISFIER_H
