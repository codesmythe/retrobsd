/*
 * Superblock routines for 2.xBSD filesystem.
 *
 * Copyright (C) 2006-2014 Serge Vakulenko, <serge@vak.ru>
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "bsdfs.h"

extern int verbose;

int fs_seek (fs_t *fs, unsigned long offset)
{
/*  printf ("seek %ld, block %ld\n", offset, offset / BSDFS_BSIZE);*/
    if (lseek (fs->fd, fs->part_offset + offset, 0) < 0) {
        if (verbose)
            printf ("error seeking %ld, block %ld\n",
                offset, offset / BSDFS_BSIZE);
        return 0;
    }
    fs->seek = offset;
    return 1;
}

int fs_read8 (fs_t *fs, unsigned char *val)
{
    if (read (fs->fd, val, 1) != 1) {
        if (verbose)
            printf ("error read8, seek %ld block %ld\n", fs->seek, fs->seek / BSDFS_BSIZE);
        return 0;
    }
    return 1;
}

int fs_read16 (fs_t *fs, unsigned short *val)
{
    unsigned char data [2];

    if (read (fs->fd, data, 2) != 2) {
        if (verbose)
            printf ("error read16, seek %ld block %ld\n", fs->seek, fs->seek / BSDFS_BSIZE);
        return 0;
    }
    *val = data[1] << 8 | data[0];
    return 1;
}

int fs_read32 (fs_t *fs, unsigned *val)
{
    unsigned char data [4];

    if (read (fs->fd, data, 4) != 4) {
        if (verbose)
            printf ("error read32, seek %ld block %ld\n", fs->seek, fs->seek / BSDFS_BSIZE);
        return 0;
    }
    *val = (unsigned long) data[0] | (unsigned long) data[1] << 8 |
        data[2] << 16 | data[3] << 24;
    return 1;
}

int fs_write8 (fs_t *fs, unsigned char val)
{
    if (write (fs->fd, &val, 1) != 1)
        return 0;
    return 1;
}

int fs_write16 (fs_t *fs, unsigned short val)
{
    unsigned char data [2];

    data[0] = val;
    data[1] = val >> 8;
    if (write (fs->fd, data, 2) != 2)
        return 0;
    return 1;
}

int fs_write32 (fs_t *fs, unsigned val)
{
    unsigned char data [4];

    data[0] = val;
    data[1] = val >> 8;
    data[2] = val >> 16;
    data[3] = val >> 24;
    if (write (fs->fd, data, 4) != 4)
        return 0;
    return 1;
}

int fs_read (fs_t *fs, unsigned char *data, int bytes)
{
    int len;

    while (bytes > 0) {
        len = bytes;
        if (len > 16*BSDFS_BSIZE)
            len = 16*BSDFS_BSIZE;
        if (read (fs->fd, data, len) != len)
            return 0;
        data += len;
        bytes -= len;
    }
    return 1;
}

int fs_write (fs_t *fs, unsigned char *data, int bytes)
{
    int len;

    if (! fs->writable)
        return 0;
    while (bytes > 0) {
        len = bytes;
        if (len > 16*BSDFS_BSIZE)
            len = 16*BSDFS_BSIZE;
        if (write (fs->fd, data, len) != len)
            return 0;
        data += len;
        bytes -= len;
    }
    return 1;
}

int fs_set_partition (fs_t *fs, unsigned pindex)
{
    unsigned char buf [512], *entry;

    if (pindex > 4 || pindex < 1) {
        fprintf (stderr, "%s: incorrect partition index=%u\n",
            fs->filename, pindex);
        return 0;
    }
    if (read (fs->fd, buf, 512) != 512) {
        fprintf (stderr, "%s: cannot read partition table\n", fs->filename);
        return 0;
    }
    if (buf[510] != 0x55 || buf[511] != 0xaa) {
        fprintf (stderr, "%s: Warning: unexpected type of RetroBSD partition\n",
            fs->filename);
    }
    /* Read partition entry. */
    entry = &buf [446 + (pindex-1)*16];
    fs->part_type = entry [4];
    fs->part_offset = *(unsigned*) &entry [8];
    fs->part_nsectors = *(unsigned*) &entry [12];
    if (fs->part_type == 0) {
        fprintf (stderr, "%s: Partition %u not allocated.\n",
            fs->filename, pindex);
        return 0;
    }
    if (fs->part_type != 0xb7) {
        fprintf (stderr, "%s: Warning: unexpected type of RetroBSD partition\n",
            fs->filename);
    }
    if (fs->part_offset & 1) {
        fprintf (stderr, "%s: Incorrect partition offset=%u, must be even\n",
            fs->filename, fs->part_offset);
        return 0;
    }
    fs->part_offset *= 512;
//printf ("Partition %u, type %02x, offset=%uk, length=%uk\n", pindex, fs->part_type, fs->part_offset/1024, fs->part_nsectors/2);
    return fs_seek (fs, 0);
}

int fs_open (fs_t *fs, const char *filename, int writable, unsigned pindex)
{
    int i;
    unsigned magic;

    memset (fs, 0, sizeof (*fs));
    fs->filename = filename;
    fs->seek = 0;

    fs->fd = open (fs->filename, writable ? O_RDWR : O_RDONLY);
    if (fs->fd < 0)
        return 0;
    fs->writable = writable;

    if (pindex > 0) {
        /* Get offset from partition table. */
        if (! fs_set_partition (fs, pindex))
            return 0;
    }

    if (! fs_read32 (fs, &magic) ||     /* magic word */
        magic != FSMAGIC1) {
        if (verbose)
            printf ("fs_open: bad magic1 = %08x, expected %08x\n",
                magic, FSMAGIC1);
        return 0;
    }
    if (! fs_read32 (fs, &fs->isize))   /* size in blocks of I list */
        return 0;
    if (! fs_read32 (fs, &fs->fsize))   /* size in blocks of entire volume */
        return 0;
    if (! fs_read32 (fs, &fs->swapsz))  /* size in blocks of swap area */
        return 0;
    if (! fs_read32 (fs, &fs->nfree))   /* number of in core free blocks */
        return 0;
    for (i=0; i<NICFREE; ++i) {         /* in core free blocks */
        if (! fs_read32 (fs, &fs->free[i]))
            return 0;
    }
    if (! fs_read32 (fs, &fs->ninode))  /* number of in core I nodes */
        return 0;
    for (i=0; i<NICINOD; ++i) {         /* in core free I nodes */
        if (! fs_read32 (fs, &fs->inode[i]))
            return 0;
    }
    if (! fs_read32 (fs, &fs->flock))
        return 0;
    if (! fs_read32 (fs, &fs->fmod))
        return 0;
    if (! fs_read32 (fs, &fs->ilock))
        return 0;
    if (! fs_read32 (fs, &fs->ronly))
        return 0;
    if (! fs_read32 (fs, (unsigned*) &fs->utime))
        return 0;                       /* current date of last update */
    if (! fs_read32 (fs, &fs->tfree))   /* total free blocks */
        return 0;
    if (! fs_read32 (fs, &fs->tinode))  /* total free inodes */
        return 0;
    if (! fs_read (fs, (unsigned char*) fs->fsmnt, MAXMNTLEN))
        return 0;                       /* ordinary file mounted on */
    if (! fs_read32 (fs, &fs->lasti))   /* start place for circular search */
        return 0;
    if (! fs_read32 (fs, &fs->nbehind)) /* est # free inodes before s_lasti */
        return 0;
    if (! fs_read32 (fs, &fs->flags))   /* mount time flags */
        return 0;
    if (! fs_read32 (fs, &magic) ||     /* magic word */
        magic != FSMAGIC2) {
        if (verbose)
            printf ("fs_open: bad magic2 = %08x, expected %08x\n",
                magic, FSMAGIC2);
        return 0;
    }
    return 1;
}

int fs_sync (fs_t *fs, int force)
{
    int i;

    if (! fs->writable)
        return 0;
    if (! force && ! fs->dirty)
        return 1;

    time (&fs->utime);
    if (! fs_seek (fs, 0))
        return 0;

    if (! fs_write32 (fs, FSMAGIC1))    /* magic word */
        return 0;
    if (! fs_write32 (fs, fs->isize))   /* size in blocks of I list */
        return 0;
    if (! fs_write32 (fs, fs->fsize))   /* size in blocks of entire volume */
        return 0;
    if (! fs_write32 (fs, fs->swapsz))  /* size in blocks of swap area */
        return 0;
    if (! fs_write32 (fs, fs->nfree))   /* number of in core free blocks */
        return 0;
    for (i=0; i<NICFREE; ++i) {         /* in core free blocks */
        if (! fs_write32 (fs, fs->free[i]))
            return 0;
    }
    if (! fs_write32 (fs, fs->ninode))  /* number of in core I nodes */
        return 0;
    for (i=0; i<NICINOD; ++i) {         /* in core free I nodes */
        if (! fs_write32 (fs, fs->inode[i]))
            return 0;
    }
    if (! fs_write32 (fs, fs->flock))
        return 0;
    if (! fs_write32 (fs, fs->fmod))
        return 0;
    if (! fs_write32 (fs, fs->ilock))
        return 0;
    if (! fs_write32 (fs, fs->ronly))
        return 0;
    if (! fs_write32 (fs, fs->utime))   /* current date of last update */
        return 0;
    if (! fs_write32 (fs, fs->tfree))   /* total free blocks */
        return 0;
    if (! fs_write32 (fs, fs->tinode))  /* total free inodes */
        return 0;
    if (! fs_write (fs, (unsigned char*) fs->fsmnt, MAXMNTLEN))
        return 0;                       /* ordinary file mounted on */
    if (! fs_write32 (fs, 0))           /* lasti*/
        return 0;
    if (! fs_write32 (fs, 0))           /* nbehind */
        return 0;
    if (! fs_write32 (fs, 0))           /* flags */
        return 0;
    if (! fs_write32 (fs, FSMAGIC2))    /* magic word */
        return 0;
    fs->dirty = 0;
    return 1;
}

void fs_print (fs_t *fs, FILE *out)
{
    int i;

    fprintf (out, "                File: %s\n", fs->filename);
    fprintf (out, "         Volume size: %u blocks\n", fs->fsize);
    fprintf (out, "     Inode list size: %u blocks\n", fs->isize);
    fprintf (out, "           Swap size: %u blocks\n", fs->swapsz);
    fprintf (out, "   Total free blocks: %u blocks\n", fs->tfree);
    fprintf (out, "   Total free inodes: %u inodes\n", fs->tinode);
    fprintf (out, "     Last mounted on: %.*s\n", MAXMNTLEN,
        fs->fsmnt[0] ? fs->fsmnt : "(none)");
    fprintf (out, "   In-core free list: %u blocks", fs->nfree);
    if (verbose) {
        for (i=0; i < NICFREE && i < fs->nfree; ++i) {
            if (i % 10 == 0)
                fprintf (out, "\n                     ");
            fprintf (out, " %u", fs->free[i]);
        }
    }
    fprintf (out, "\n");

    fprintf (out, " In-core free inodes: %u inodes", fs->ninode);
    if (verbose) {
        for (i=0; i < NICINOD && i < fs->ninode; ++i) {
            if (i % 10 == 0)
                fprintf (out, "\n                     ");
            fprintf (out, " %u", fs->inode[i]);
        }
    }
    fprintf (out, "\n");
    if (verbose) {
//      fprintf (out, "      Free list lock: %u\n", fs->flock);
//      fprintf (out, "     Inode list lock: %u\n", fs->ilock);
//      fprintf (out, "Super block modified: %u\n", fs->fmod);
//      fprintf (out, "   Mounted read-only: %u\n", fs->ronly);
//      fprintf (out, "   Circ.search start: %u\n", fs->lasti);
//      fprintf (out, "  Circ.search behind: %u\n", fs->nbehind);
//      fprintf (out, "         Mount flags: 0x%x\n", fs->flags);
    }

    fprintf (out, "    Last update time: %s", ctime (&fs->utime));
}

void fs_close (fs_t *fs)
{
    if (fs->fd < 0)
        return;

    close (fs->fd);
    fs->fd = -1;
}
