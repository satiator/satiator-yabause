/*  Copyright 2015 James Laird-Wah

    This file is part of Saturn Satisfier and is not licensed under the GPL.
    This file may be freely distributed and used as part of Yabause.
    Incorporation of any part of this file in any other software or firmware
    without the written permission of the author is not permitted.
*/


// file handles {{{
static FIL files[16];
static uint16_t files_open = 0;

static FIL* file_for_handle(int8_t handle) {
    if (handle < 0)
        return NULL;
    handle &= 0xf;
    if (!(files_open & (1<<handle)))
        return NULL;
    return &files[handle];
}
static int8_t alloc_handle(void) {
    int8_t handle = 0;
    for (handle=0; handle<16; handle++) {
        if (!(files_open & (1<<handle))) {
            files_open |= (1<<handle);
            return handle;
        }
    }
    return -1;
}
static void free_handle(int8_t handle) {
    if (handle < 0)
        return;
    handle &= 0xf;
    files_open &= ~(1<<handle);
}
// }}}

typedef struct {
    uint32_t size;
    uint16_t date, time;
    uint8_t attr;
    char name[];
} __attribute__((packed)) s_stat_t;

static uint8_t buffer[2049];
uint16_t cdb_buf[5];
extern FIL cdb_descfile;

static uint32_t fat_time = 0;
DWORD get_fattime(void) {
    if (fat_time)
        return fat_time;
    else
        return (35<<25) |   // 2015
               (3<<21)  |   // March
               (1<<16)  |   // the first
               0;           // midnight
}

static inline void set_status(uint8_t result) {
    cdb_buf[0] = result << 8;
}
static inline void set_length(uint32_t length) {
    cdb_buf[2] = length>>16;
    cdb_buf[3] = length;
}
static inline uint32_t get_length(void) {
    return (cdb_buf[2] << 16) | cdb_buf[3];
}
static inline uint8_t get_handle(void) {
    return cdb_buf[0] & 0xf;
}
static inline FIL *get_file(void) {
    return file_for_handle(get_handle());
}

#define read_filename()     do {                    \
        if (length > 256)                           \
            goto fail;                              \
        read_buf(length);                           \
        buffer[length] = '\0';                      \
    } while(0)

int cdb_command(void) {
    static FIL *f;
    static FRESULT result;
    static uint8_t handle;
    static uint32_t length;
    static UINT nread, nwritten;
    static int bios_len;
    static FIL biosfp;
    static FILINFO stat;
    static uint8_t lfname[256];
    static DIR dir;
    static s_stat_t *sst;
    static int32_t offset;

    int cmd;
    setup_cmd();

    switch (cmd) {
        case 0xE2:
            if (f_open(&biosfp, "menu.bin", FA_READ) != FR_OK) {
                warn("Failed to open menu.bin!");
                goto fail;
            }
            f_lseek(&biosfp, cdb_buf[1] * 2048);
            if (f_tell(&biosfp) != cdb_buf[1] * 2048)
                goto fail;
            bios_len = cdb_buf[3];
            while (bios_len--) {
                f_read(&biosfp, buffer, 2048, &nread);
                write_bios_sector();
            }
            f_close(&biosfp);
            break;

        case c_close:
            if (!f)
                goto fail;
            result = f_close(f);
            set_status(result);
            free_handle(handle);
            break;

        case c_read:
            if (!f)
                goto fail;
            if (length > sizeof(buffer))
                goto fail;
            result = f_read(f, buffer, length, &nread);
            set_length(nread);
            set_status(result);
            if (result == FR_OK)
                write_buf(nread);
            break;

        case c_seek:
            if (!f)
                goto fail;
            offset = get_length();
            switch (cdb_buf[1]) {
                case C_SEEK_SET:
                    result = f_lseek(f, offset);
                    break;
                case C_SEEK_CUR:
                    result = f_lseek(f, offset + f_tell(f));
                    break;
                case C_SEEK_END:
                    result = f_lseek(f, offset + f_size(f));
                    break;
                default:
                    result = FR_INVALID_PARAMETER;
            }
            set_length(f_tell(f));
            set_status(result);
            break;

        case c_open:
            read_filename();
            handle = alloc_handle();
            if (handle < 0) {
                set_status(FR_TOO_MANY_OPEN_FILES);
                return;
            }
            f = file_for_handle(handle);
            result = f_open(f, buffer, cdb_buf[1] & 0xff);
            if (result != FR_OK) {
                free_handle(handle);
                cdb_buf[3] = 0xffff;
            } else {
                cdb_buf[3] = handle;
            }
            set_status(result);
            break;

        case c_write:
            if (!f) {
                set_status(FR_INVALID_OBJECT);
                break;
            }
            read_buf(length);
            result = f_write(f, buffer, length, &nwritten);
            set_length(nwritten);
            set_status(result);
            break;

        case c_truncate:
            if (!f) {
                set_status(FR_INVALID_OBJECT);
                break;
            }
            result = f_truncate(f);
            set_length(f_tell(f));
            set_status(result);
            break;

        case c_readdir:
            stat.lfname = lfname;
            stat.lfsize = sizeof(lfname);
            result = f_readdir(&dir, &stat);
            set_status(result);
            if (result)
                break;
            goto return_stat;

        case c_stat:
            read_filename();
            stat.lfname = lfname;
            stat.lfsize = sizeof(lfname);
            result = f_stat(buffer, &stat);
            set_status(result);
            if (result)
                break;
return_stat:
            // 9 bytes as per FILINFO, but with endianness fixed
            sst = (void*)buffer;
            sst->size = htonl(stat.fsize);
            sst->date = htons(stat.fdate);
            sst->time = htons(stat.ftime);
            sst->attr = stat.fattrib;
            // then the filename
            if (*stat.lfname) {
                strcpy(sst->name, stat.lfname);
            } else {
                strcpy(sst->name, stat.fname);
            }
            set_length(9 + strlen(sst->name));
            break;

        case c_rename:
            // two filenames, NUL separated
            read_filename();
            result = f_rename(buffer, buffer+strlen(buffer)+1);
            set_status(result);
            break;

        case c_unlink:
            read_filename();
            result = f_unlink(buffer);
            set_status(result);
            break;

        case c_mkdir:
            read_filename();
            result = f_mkdir(buffer);
            set_status(result);
            break;

        case c_opendir:
            read_filename();
            result = f_opendir(&dir, buffer);
            set_status(result);
            break;

        case c_chdir:
            read_filename();
            result = f_chdir(buffer);
            set_status(result);
            f_getcwd(buffer, sizeof(buffer));
            set_length(strlen(buffer));
            break;

#if _USE_MKFS
        case c_mkfs:
            if (cdb_buf[1] != 0xfeed ||
                cdb_buf[2] != 0xdead ||
                cdb_buf[3] != 0xbeef)
                goto fail;
            result = f_mkfs("", 0, 0);
            set_status(result);
            break;
#endif

        case c_settime:
            fat_time = length;
            break;

        case c_emulate:
            f_close(&cdb_descfile);
            read_filename();
            result = f_open(&cdb_descfile, buffer, FA_READ);
            set_status(result);
            cdb_buf[2] = 0x5555;
            cdb_buf[3] = 0xaaaa;
            if (result == FR_OK)
                start_emulation(buffer);
            break;

        default:    // unknown cmd
            goto fail;
    }

    if (0) {
fail:
        cdb_buf[0] = cdb_buf[1] = cdb_buf[2] = cdb_buf[3] = 0xffff;
    }

    finish_cmd();
}
