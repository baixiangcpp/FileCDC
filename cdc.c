#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <glib/gstdio.h>
#include "cdc.h"
#include "checksum.h"
#include "utils.h"

#define finger rabin_checksum
#define rolling_finger rabin_rolling_checksum

#define BLOCK_SZ        (1024*1024*1)
#define BLOCK_MIN_SZ    (1024*256)
#define BLOCK_MAX_SZ    (1024*1024*4)
#define BLOCK_WIN_SZ    48

#define NAME_MAX_SZ     4096

#define BREAK_VALUE     0x0013    ///0x0513

#define READ_SIZE 1024 * 4

#define BYTE_TO_HEX(b)  (((b)>=10)?('a'+b-10):('0'+b))

int write_chunk ( CDCDescriptor *chunk, uint8_t *checksum)
{
    GChecksum *ctx = g_checksum_new (G_CHECKSUM_SHA1);
    gsize len = 20;
    int ret = 0;

    g_checksum_update (ctx, (unsigned char *)chunk->block_buf, chunk->len);
    g_checksum_get_digest (ctx, checksum, &len);

    ret = do_write_chunk (checksum, chunk->block_buf, chunk->len);
    
    g_checksum_free (ctx);

    return ret;
}

static int init_cdc_file_descriptor (int fd,
                                     uint64_t file_size,
                                     CDCFileDescriptor *file_descr)
{
    int max_block_nr = 0;
    int block_min_sz = 0;

    file_descr->file_size = 0;

    file_descr->block_nr = 0;

    if (file_descr->block_min_sz <= 0)
        file_descr->block_min_sz = BLOCK_MIN_SZ;
    if (file_descr->block_max_sz <= 0)
        file_descr->block_max_sz = BLOCK_MAX_SZ;
    if (file_descr->block_sz <= 0)
        file_descr->block_sz = BLOCK_SZ;

    if (file_descr->write_block == NULL)
        file_descr->write_block = (WriteblockFunc)write_chunk;

    block_min_sz = file_descr->block_min_sz;
    max_block_nr = ((file_size + block_min_sz - 1) / block_min_sz);
    file_descr->blk_sha1s = (uint8_t *)calloc (sizeof(uint8_t),
                                               max_block_nr * CHECKSUM_LENGTH);
    file_descr->max_block_nr = max_block_nr;

    return 0;
}

#define WRITE_CDC_BLOCK(block_sz)                            \
do {                                                         \
    int _block_sz = (block_sz);                              \
    chunk_descr.len = _block_sz;                             \
    chunk_descr.offset = offset;                             \
    ret = file_descr->write_block (&chunk_descr,             \
                                   chunk_descr.checksum);    \
    if (ret < 0) {                                           \
        free (buf);                                          \
        g_warning ("CDC: failed to write chunk.\n");         \
        return -1;                                           \
    }                                                        \
    memcpy (file_descr->blk_sha1s +                          \
            file_descr->block_nr * CHECKSUM_LENGTH,          \
            chunk_descr.checksum, CHECKSUM_LENGTH);          \
    g_checksum_update (file_ctx, chunk_descr.checksum, 20);       \
    file_descr->block_nr++;                                  \
    offset += _block_sz;                                     \
                                                             \
    memmove (buf, buf + _block_sz, tail - _block_sz);        \
    tail = tail - _block_sz;                                 \
    cur = 0;                                                 \
}while(0);

/* content-defined chunking */
int file_chunk_cdc(int fd_src, CDCFileDescriptor *file_descr)
{
    char *buf = NULL;
    uint32_t buf_sz;
    GChecksum *file_ctx = g_checksum_new (G_CHECKSUM_SHA1);
    CDCDescriptor chunk_descr;
    int ret = 0;

    struct stat st;
    if (fstat (fd_src, &st) < 0) {
        printf ("CDC: failed to stat: %s.\n", strerror(errno));
        ret = -1;
        goto out;
    }
    uint64_t expected_size = st.st_size;

    init_cdc_file_descriptor (fd_src, expected_size, file_descr);
    uint32_t block_min_sz = file_descr->block_min_sz;
    uint32_t block_mask = file_descr->block_sz - 1;

    int fingerprint = 0;
    int offset = 0;
    int tail, cur, rsize;

    buf_sz = file_descr->block_max_sz;
    buf = chunk_descr.block_buf = malloc (buf_sz);
    if (!buf) {
        ret = -1;
        goto out;
    }

    /* buf: a fix-sized buffer.
     * cur: data behind (inclusive) this offset has been scanned.
     *      cur + 1 is the bytes that has been scanned.
     * tail: length of data loaded into memory. buf[tail] is invalid.
     */
    tail = cur = 0;
    while (1) {
        if (tail < block_min_sz) {
            rsize = block_min_sz - tail + READ_SIZE;
        } else {
            rsize = (buf_sz - tail < READ_SIZE) ? (buf_sz - tail) : READ_SIZE;
        }
        ret = readn (fd_src, buf + tail, rsize);
        if (ret < 0) {
            printf ("CDC: failed to read: %s.\n", strerror(errno));
            ret = -1;
            goto out;
        }
        tail += ret;
        file_descr->file_size += ret;

        if (file_descr->file_size > expected_size) {
            printf ("File size changed while chunking.\n");
            ret = -1;
            goto out;
        }

        /* We've read all the data in this file. Output the block immediately
         * in two cases:
         * 1. The data left in the file is less than block_min_sz;
         * 2. We cannot find the break value until the end of this file.
         */
        if (tail < block_min_sz || cur >= tail) {
            if (tail > 0) {
                if (file_descr->block_nr == file_descr->max_block_nr) {
                    printf ("Block id array is not large enough, bail out.\n");
                    ret = -1;
                    goto out;
                }
                WRITE_CDC_BLOCK (tail);
            }
            break;
        }

        /* 
         * A block is at least of size block_min_sz.
         */
        if (cur < block_min_sz - 1)
            cur = block_min_sz - 1;

        while (cur < tail) {
            fingerprint = (cur == block_min_sz - 1) ?
                finger(buf + cur - BLOCK_WIN_SZ + 1, BLOCK_WIN_SZ) :
                rolling_finger (fingerprint, BLOCK_WIN_SZ, 
                                *(buf+cur-BLOCK_WIN_SZ), *(buf + cur));

            /* get a chunk, write block info to chunk file */
            if (((fingerprint & block_mask) ==  ((BREAK_VALUE & block_mask)))
                || cur + 1 >= file_descr->block_max_sz)
            {
                if (file_descr->block_nr == file_descr->max_block_nr) {
                    printf ("Block id array is not large enough, bail out.\n");
                    ret = -1;
                    goto out;
                }

                WRITE_CDC_BLOCK (cur + 1);
                break;
            } else {
                cur ++;
            }
        }
    }

    gsize chk_sum_len = CHECKSUM_LENGTH;
    g_checksum_get_digest (file_ctx, file_descr->file_sum, &chk_sum_len);

out:
    free (buf);
    g_checksum_free (file_ctx);

    return ret;
}

int filename_chunk_cdc(const char *filename,CDCFileDescriptor *file_descr)
{
    int fd_src = open (filename, O_RDONLY | O_BINARY);
    if (fd_src < 0) {
        printf ("CDC: failed to open %s.\n", filename);
        return -1;
    }

    int ret = file_chunk_cdc (fd_src, file_descr);
    close (fd_src);
    return ret;
}

void cdc_init ()
{
    rabin_init (BLOCK_WIN_SZ);
}

#define CDC_AVERAGE_BLOCK_SIZE (1 << 23) /* 8MB */
#define CDC_MIN_BLOCK_SIZE (6 * (1 << 20)) /* 6MB */
#define CDC_MAX_BLOCK_SIZE (10 * (1 << 20)) /* 10MB */

void cdc_work(const char* filename)
{
    CDCFileDescriptor cdc;

    cdc.block_sz = CDC_AVERAGE_BLOCK_SIZE;
    cdc.block_min_sz = CDC_MIN_BLOCK_SIZE;
    cdc.block_max_sz = CDC_MAX_BLOCK_SIZE;
    cdc.write_block = write_chunk;

    filename_chunk_cdc(filename,&cdc);
}
