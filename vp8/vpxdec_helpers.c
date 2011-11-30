/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


static unsigned int mem_get_le16(const void *vmem)
{
    unsigned int  val;
    const unsigned char *mem = (const unsigned char *)vmem;

    val = mem[1] << 8;
    val |= mem[0];
    return val;
}

static unsigned int mem_get_le32(const void *vmem)
{
    unsigned int  val;
    const unsigned char *mem = (const unsigned char *)vmem;

    val = mem[3] << 24;
    val |= mem[2] << 16;
    val |= mem[1] << 8;
    val |= mem[0];
    return val;
}

enum file_kind
{
    RAW_FILE,
    IVF_FILE,
    WEBM_FILE
};

struct input_ctx
{
    enum file_kind  kind;
    FILE           *infile;
    nestegg        *nestegg_ctx;
    nestegg_packet *pkt;
    unsigned int    chunk;
    unsigned int    chunks;
    unsigned int    video_track;
};

#define IVF_FRAME_HDR_SZ (sizeof(uint32_t) + sizeof(uint64_t))
#define RAW_FRAME_HDR_SZ (sizeof(uint32_t))
static int read_frame(struct input_ctx      *input,
                      uint8_t               **buf,
                      size_t                *buf_sz,
                      size_t                *buf_alloc_sz)
{
    char            raw_hdr[IVF_FRAME_HDR_SZ];
    size_t          new_buf_sz;
    FILE           *infile = input->infile;
    enum file_kind  kind = input->kind;
    if(kind == WEBM_FILE)
    {
        if(input->chunk >= input->chunks)
        {
            unsigned int track;

            do
            {
                /* End of this packet, get another. */
                if(input->pkt)
                    nestegg_free_packet(input->pkt);

                if(nestegg_read_packet(input->nestegg_ctx, &input->pkt) <= 0
                   || nestegg_packet_track(input->pkt, &track))
                    return 1;

            } while(track != input->video_track);

            if(nestegg_packet_count(input->pkt, &input->chunks))
                return 1;
            input->chunk = 0;
        }

        if(nestegg_packet_data(input->pkt, input->chunk, buf, buf_sz))
            return 1;
        input->chunk++;

        return 0;
    }
    /* For both the raw and ivf formats, the frame size is the first 4 bytes
     * of the frame header. We just need to special case on the header
     * size.
     */
    else if (fread(raw_hdr, kind==IVF_FILE
                   ? IVF_FRAME_HDR_SZ : RAW_FRAME_HDR_SZ, 1, infile) != 1)
    {
        if (!feof(infile))
            fprintf(stderr, "Failed to read frame size\n");

        new_buf_sz = 0;
    }
    else
    {
        new_buf_sz = mem_get_le32(raw_hdr);

        if (new_buf_sz > 256 * 1024 * 1024)
        {
            fprintf(stderr, "Error: Read invalid frame size (%u)\n",
                    (unsigned int)new_buf_sz);
            new_buf_sz = 0;
        }

        if (kind == RAW_FILE && new_buf_sz > 256 * 1024)
            fprintf(stderr, "Warning: Read invalid frame size (%u)"
                    " - not a raw file?\n", (unsigned int)new_buf_sz);

        if (new_buf_sz > *buf_alloc_sz)
        {
            uint8_t *new_buf = (uint8_t *)realloc(*buf, 2 * new_buf_sz);

            if (new_buf)
            {
                *buf = new_buf;
                *buf_alloc_sz = 2 * new_buf_sz;
            }
            else
            {
                fprintf(stderr, "Failed to allocate compressed data buffer\n");
                new_buf_sz = 0;
            }
        }
    }

    *buf_sz = new_buf_sz;

    if (!feof(infile))
    {
        if (fread(*buf, 1, *buf_sz, infile) != *buf_sz)
        {
            fprintf(stderr, "Failed to read full frame\n");
            return 1;
        }

        return 0;
    }

    return 1;
}

#define IVF_FRAME_HDR_SZ (sizeof(uint32_t) + sizeof(uint64_t))
#define RAW_FRAME_HDR_SZ (sizeof(uint32_t))
unsigned int file_is_ivf(FILE *infile,
                         unsigned int *fourcc,
                         unsigned int *width,
                         unsigned int *height,
                         unsigned int *fps_den,
                         unsigned int *fps_num)
{
    char raw_hdr[32];
    int is_ivf = 0;

    if (fread(raw_hdr, 1, 32, infile) == 32)
    {
        if (raw_hdr[0] == 'D' && raw_hdr[1] == 'K'
            && raw_hdr[2] == 'I' && raw_hdr[3] == 'F')
        {
            is_ivf = 1;

            if (mem_get_le16(raw_hdr + 4) != 0)
                fprintf(stderr, "Error: Unrecognized IVF version! This file may not"
                        " decode properly.");

            *fourcc = mem_get_le32(raw_hdr + 8);
            *width = mem_get_le16(raw_hdr + 12);
            *height = mem_get_le16(raw_hdr + 14);
            *fps_num = mem_get_le32(raw_hdr + 16);
            *fps_den = mem_get_le32(raw_hdr + 20);

            /* Some versions of vpxenc used 1/(2*fps) for the timebase, so
             * we can guess the framerate using only the timebase in this
             * case. Other files would require reading ahead to guess the
             * timebase, like we do for webm.
             */
            if(*fps_num < 1000)
            {
                /* Correct for the factor of 2 applied to the timebase in the
                 * encoder.
                 */
                if(*fps_num&1)*fps_den<<=1;
                else *fps_num>>=1;
            }
            else
            {
                /* Don't know FPS for sure, and don't have readahead code
                 * (yet?), so just default to 30fps.
                 */
                *fps_num = 30;
                *fps_den = 1;
            }
        }
    }

    if (!is_ivf)
        rewind(infile);

    return is_ivf;
}


unsigned int file_is_raw(FILE *infile,
                         unsigned int *fourcc,
                         unsigned int *width,
                         unsigned int *height,
                         unsigned int *fps_den,
                         unsigned int *fps_num)
{
    unsigned char buf[32];
    int is_raw = 0;
    vpx_codec_stream_info_t si;

    si.sz = sizeof(si);

    if (fread(buf, 1, 32, infile) == 32)
    {
        int i;

        if(mem_get_le32(buf) < 256 * 1024 * 1024)
            for (i = 0; i < sizeof(ifaces) / sizeof(ifaces[0]); i++)
                if(!vpx_codec_peek_stream_info(ifaces[i].iface,
                                               buf + 4, 32 - 4, &si))
                {
                    is_raw = 1;
                    *fourcc = ifaces[i].fourcc;
                    *width = si.w;
                    *height = si.h;
                    *fps_num = 30;
                    *fps_den = 1;
                    break;
                }
    }

    rewind(infile);
    return is_raw;
}


int
nestegg_read_cb(void *buffer, size_t length, void *userdata)
{
    FILE *f = (FILE *)userdata;

    if(fread(buffer, 1, length, f) < length)
    {
        if (ferror(f))
            return -1;
        if (feof(f))
            return 0;
    }
    /* fprintf(stderr,"Read %d bytes: ",length); 
    while (length > 0) {
      fprintf(stderr,"%hhx",*(char*)buffer);
      length--;
      buffer++;
    }
    fprintf(stderr,"\n");*/
    return 1;
}


int
nestegg_seek_cb(int64_t offset, int whence, void * userdata)
{
    switch(whence) {
        case NESTEGG_SEEK_SET: whence = SEEK_SET; break;
        case NESTEGG_SEEK_CUR: whence = SEEK_CUR; break;
        case NESTEGG_SEEK_END: whence = SEEK_END; break;
    };
    return fseek((FILE *)userdata, offset, whence)? -1 : 0;
}


int64_t
nestegg_tell_cb(void * userdata)
{
    return ftell((FILE *)userdata);
}


void
nestegg_log_cb(nestegg * context, unsigned int severity, char const * format,
               ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}


int
webm_guess_framerate(struct input_ctx *input,
                     unsigned int     *fps_den,
                     unsigned int     *fps_num)
{
    unsigned int i;
    uint64_t     tstamp=0;

    /* Guess the framerate. Read up to 1 second, or 50 video packets,
     * whichever comes first.
     */
    for(i=0; tstamp < 1000000000 && i < 50;)
    {
        nestegg_packet * pkt;
        unsigned int track;

        if(nestegg_read_packet(input->nestegg_ctx, &pkt) <= 0)
            break;

        nestegg_packet_track(pkt, &track);
        if(track == input->video_track)
        {
            nestegg_packet_tstamp(pkt, &tstamp);
            i++;
        }

        nestegg_free_packet(pkt);
    }

    if(nestegg_track_seek(input->nestegg_ctx, input->video_track, 0))
        goto fail;

    *fps_num = (i - 1) * 1000000;
    *fps_den = tstamp / 1000;
    return 0;
fail:
    nestegg_destroy(input->nestegg_ctx);
    input->nestegg_ctx = NULL;
    rewind(input->infile);
    return 1;
}

int
file_is_webm(struct input_ctx *input,
             unsigned int     *fourcc,
             unsigned int     *width,
             unsigned int     *height,
             unsigned int     *fps_den,
             unsigned int     *fps_num)
{
    unsigned int i, n;
    int          track_type = -1;

    nestegg_io io = {nestegg_read_cb, nestegg_seek_cb, nestegg_tell_cb,
                     input->infile};
    nestegg_video_params params;

    if(nestegg_init(&input->nestegg_ctx, io, NULL))
    {
        fprintf(stderr, "Failed to initialize nestegg io.\n");
        goto fail;
    }

    if(nestegg_track_count(input->nestegg_ctx, &n))
    {
        fprintf(stderr, "Failed to count tracks.\n");
        goto fail;
    }

    for(i=0; i<n; i++)
    {
        track_type = nestegg_track_type(input->nestegg_ctx, i);

        if(track_type == NESTEGG_TRACK_VIDEO)
            break;
        else if(track_type < 0)
        {
            fprintf(stderr, "Failed to locate video track.\n");
            goto fail;
        }
    }

    if(nestegg_track_codec_id(input->nestegg_ctx, i) != NESTEGG_CODEC_VP8)
    {
        fprintf(stderr, "Not VP8 video, quitting.\n");
        exit(1);
    }

    input->video_track = i;

    if(nestegg_track_video_params(input->nestegg_ctx, i, &params))
    {
        fprintf(stderr, "Failed to determine video parameters.\n");
        goto fail;
    }

    *fps_den = 0;
    *fps_num = 0;
    *fourcc = VP8_FOURCC;
    *width = params.width;
    *height = params.height;
    return 1;
fail:
    input->nestegg_ctx = NULL;
    rewind(input->infile);
    return 0;
}
