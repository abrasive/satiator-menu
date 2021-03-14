/*  Copyright (c) 2015 James Laird-Wah
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/. */

#include <iapetus.h>
#include "satiator.h"
#include <string.h>
#include <stdio.h>

// I/O primitives   {{{

typedef uint16_t cmd_t[4];

void exec_cmd(cmd_t cr, uint16_t wait) {
    CDB_REG_HIRQ = ~(HIRQ_CMOK | wait);
    CDB_REG_CR1 = cr[0];
    CDB_REG_CR2 = cr[1];
    CDB_REG_CR3 = cr[2];
    CDB_REG_CR4 = cr[3];
    while (!(CDB_REG_HIRQ & HIRQ_CMOK));
    if (wait)
        while (!(CDB_REG_HIRQ & wait));
}

static uint16_t sat_result[4];
static inline void get_stat(void) {
    cmd_t cmd = {c_get_status<<8, 0, 0, 0};
    exec_cmd(cmd, 0);
    sat_result[0] = CDB_REG_CR1;
    sat_result[1] = CDB_REG_CR2;
    sat_result[2] = CDB_REG_CR3;
    sat_result[3] = CDB_REG_CR4;
}

static inline int buffer_xfer(void *buf, int len, int dir) {
    uint8_t cmdbyte = dir ? c_write_buffer : c_read_buffer;
    cmd_t cmd = {cmdbyte<<8, 0, 0, len};
    exec_cmd(cmd, HIRQ_EHST);
    if (CDB_REG_CR1)  // error
        return -1;

    while (!(CDB_REG_HIRQ & HIRQ_DRDY));

    uint32_t *p = buf;
    uint16_t n = (len+3)/4;
    if (dir) {
        while (n--)
            CDB_REG_DATATRNS = *p++;
        // mandatory but mysterious
        CDB_REG_DATATRNS = 0;
        CDB_REG_DATATRNS = 0;
    } else {
        while (n--)
            *p++ = CDB_REG_DATATRNS;
    }

    return 0;
}

#define buffer_read(buf, len)  buffer_xfer(buf, len, 0)
#define buffer_write(buf, len) buffer_xfer((void*)(uintptr_t)buf, len, 1)

// }}}

// Convenience functions {{{
// Most calls use a similar pattern
static inline void set_cmd(cmd_t cmd, int op, int fd, int flags, int len) {
    cmd[0] = (op<<8) | (fd & 0xff);
    cmd[1] = flags;
    cmd[2] = len>>16;
    cmd[3] = len;
}

// Most API returns a negative number for errors
#define get_check_stat() do {               \
    get_stat();                             \
    uint8_t __retval = sat_result[0] >> 8;  \
    if (__retval)                           \
        return -__retval;                   \
} while(0);

#define get_length()    ((sat_result[2]<<16) | sat_result[3])

// Most calls use the same sequence
#define simplecall(...) do {                \
    cmd_t cmd;                              \
    set_cmd(cmd, __VA_ARGS__);              \
    exec_cmd(cmd, HIRQ_MPED);               \
    get_check_stat();                       \
} while(0);

// }}}

// File API {{{
// Given a filename and some FA_xxx flags, return a file descriptor
// (or a negative error corresponding to FR_xxx)
int s_open(const char *filename, int flags) {
    buffer_write(filename, strlen(filename));
    simplecall(c_open, 0, flags, strlen(filename));
    return sat_result[3];   // handle
}

// Close a file descriptor
int s_close(int fd) {
    simplecall(c_close, fd, 0, 0);
    return 0;
}

// Seek to a byte on an fd. Returns the offset
int s_seek(int fd, int offset, int whence) {
    simplecall(c_seek, fd, whence, offset);
    return (sat_result[2]<<16) | sat_result[3];
}

// Read some data. Returns bytes read
int s_read(int fd, void *buf, int len) {
    if (len > S_MAXBUF || len < 0)
        return -FR_INVALID_PARAMETER;
    simplecall(c_read, fd, 0, len);
    len = get_length();
    buffer_read(buf, len);
    return len;
}

// Write some data. Returns bytes written
int s_write(int fd, const void *buf, int len) {
    if (len > S_MAXBUF || len < 0)
        return -FR_INVALID_PARAMETER;
    buffer_write(buf, len);
    simplecall(c_write, fd, 0, len);
    return get_length();
}

// Flush any buffered data to file
int s_sync(int fd) {
    return s_seek(fd, 0, C_SEEK_CUR);
}

// Truncate file at current pointer. Returns new length
int s_truncate(int fd) {
    simplecall(c_truncate, fd, 0, 0);
    return get_length();
}

// Get info on a named file. Pass in a pointer to a buffer and
// its size - the filename may be truncated if the buffer is
// short.  Returns the length of the (truncated) filename.
// If the filename is NULL, reads the next file from the
// current directory (readdir).
int s_stat(const char *filename, s_stat_t *stat, int statsize) {
    int len;
    if (statsize < 9)
        return -FR_INVALID_PARAMETER;
    if (filename) {
        len = strlen(filename);
        buffer_write(filename, len);
    } else {
        len = 0;
    }
    simplecall(filename ? c_stat : c_readdir, 0, 0, len);
    len = get_length();
    if (len > statsize)
        len = statsize;
    buffer_read(stat, len);
    return len - 9;
}

// Rename a file.
int s_rename(const char *old, const char *new) {
    // Need both names, zero separated, for the write
    char namebuf[512];  // nasty huh
    int len1 = strlen(old);
    int len2 = strlen(new);
    memcpy(namebuf, old, len1+1);
    memcpy(namebuf+len1+1, new, len2);
    buffer_write(namebuf, len1+1+len2);
    simplecall(c_rename, 0, 0, len1+1+len2);
    return 0;
}

// Create a directory.
int s_mkdir(const char *filename) {
    int len = strlen(filename);
    buffer_write(filename, len);
    simplecall(c_mkdir, 0, 0, len);
    return 0;
}

// Delete a file.
int s_unlink(const char *filename) {
    int len = strlen(filename);
    buffer_write(filename, len);
    simplecall(c_unlink, 0, 0, len);
    return 0;
}

// Open a directory to read file entries.
int s_opendir(const char *filename) {
    int len = strlen(filename);
    buffer_write(filename, len);
    simplecall(c_opendir, 0, 0, len);
    return 0;
}

// Change working directory.
int s_chdir(const char *filename) {
    int len = strlen(filename);
    buffer_write(filename, len);
    simplecall(c_chdir, 0, 0, len);
    return 0;
}

// Get working directory. Adds terminating null.
int s_getcwd(char *filename, int buflen) {
    buffer_write(".", 1);
    simplecall(c_chdir, 0, 0, 1);
    buflen--;
    if (buflen > get_length())
        buflen = get_length();
    buffer_read(filename, buflen);
    filename[buflen] = 0;
    return buflen;
}

// Set the Satiator RTC. Takes a FAT timestamp.
int s_settime(uint32_t time) {
    simplecall(c_settime, 0, 0, time);
    return 0;
}
// }}}

// System API {{{
static int is_satiator_active(void) {
    // This checks the MPEG version field.
    // Real MPEG cards have version 1.
    // The Satiator has version 2.
    cmd_t cmd = {0x0100, 0, 0, 0};
    exec_cmd(cmd, 0);
    return (CDB_REG_CR3 & 0xff) == 2;
}

static enum satiator_mode cur_mode = s_cdrom;
int s_mode(enum satiator_mode mode) {
    /* Switch between emulating a CD drive and exposing the SD card API.
     * This function returns:
     *      0: success
     *      -1: Satiator not detected
     */

    if (mode == cur_mode)
        return 0;

    if (mode == s_cdrom) {
        cmd_t cmd = {0x9300, 1, 0, 0};
        exec_cmd(cmd, HIRQ_MPED);
    } else {
        cmd_t cmd = {0xe000, 0x0000, 0x00c1, 0x05e7};
        exec_cmd(cmd, HIRQ_EFLS);

        // is there actually a Satiator attached?
        if (!is_satiator_active())
            return -1;

        cd_stop_drive();
    }
    cur_mode = mode;
    return 0;
}

// Given the filename of a disc descriptor, try and boot into it.
int boot_disc(void);
int s_emulate(const char *filename) {
    int len = strlen(filename);
    buffer_write(filename, len);
    simplecall(c_emulate, 0, 0, len);
    return 0;
}

int s_get_fw_version(char *buf, int buflen) {
    simplecall(c_info, i_fw_version, 0, 0);
    buflen--;
    if (buflen > get_length())
        buflen = get_length();
    buffer_read(buf, buflen);
    buf[buflen] = 0;
    return buflen;
}

int s_get_bootloader_version(uint32_t *version) {
    simplecall(c_info, i_bootloader_version, 0, 0);
    buffer_read(version, 4);
    return 0;
}

int s_get_serial_number(uint32_t *serial) {
    simplecall(c_info, i_serial_number, 0, 0);
    buffer_read(serial, 4);
    return 0;
}

int s_get_sd_latency(uint16_t latency_us[1024], int *errors) {
    simplecall(c_info, i_sd_latency, 0, 0);
    buffer_read(latency_us, 2048);
    *errors = sat_result[1];
    return 0;
}

int s_format_sd_card(int flags) {
    simplecall(c_mkfs, flags, 0xfeed, 0xdeadbeef);
    return 0;
}

// }}}

// Cartridge API {{{
satiator_cart_header_t *s_find_cartridge(void) {
    uint8_t *ptr = (void*)0x02000000;
    uint8_t *end = ptr + 1048576;

    while (ptr < end) {
        if (!memcmp(ptr, "SatiatorCart", 12))
            return (void*)ptr;

        ptr += 0x100;
    }

    return NULL;
}
// }}}
