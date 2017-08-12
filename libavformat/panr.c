/*
* Panopto Raw File
* Copyright (c) 2017 Derek Sessions
*
* This file is part of FFmpeg.
*
* FFmpeg is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* FFmpeg is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with FFmpeg; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "internal.h"
#include "dshow.h"
#include "panr.h"

typedef struct PanrDemuxContext
{
    PanrSampleFileHeader file_header;
    uint8_t* format_block;
} PanrDemuxContext;

static int read_probe(AVProbeData *probe_data)
{
    if (probe_data->buf_size >= sizeof(PanrSampleFileHeader) &&
            ((uint32_t*)probe_data->buf)[0] == panr_signature)
    {
        PanrSampleFileHeader* test_header = (PanrSampleFileHeader*) probe_data->buf;
        // only V1 is supported
        if (test_header->version == 1)
        {
            return AVPROBE_SCORE_MAX;
        }
    }

    return 0;
}

static int read_header(AVFormatContext * format_ctx)
{
    PanrDemuxContext *demux_ctx = format_ctx->priv_data;
    AVIOContext     *pBuffer = format_ctx->pb;
    WAVEFORMATEX    *wave_format;
    AVStream        *avst = NULL;
    int ret = 0;

    if (avio_read(pBuffer, (uint8_t*) &demux_ctx->file_header, sizeof(PanrSampleFileHeader))
            != sizeof(PanrSampleFileHeader))
    {
        ret = AVERROR_INVALIDDATA;
        goto Cleanup;
    }

    demux_ctx->format_block = av_malloc(demux_ctx->file_header.cb_format);
    if (!demux_ctx->format_block)
    {
        ret = AVERROR(ENOMEM);
        goto Cleanup;
    }
    
    if (avio_read(pBuffer, (uint8_t*) demux_ctx->format_block, demux_ctx->file_header.cb_format)
            != demux_ctx->file_header.cb_format)
    {
        ret = AVERROR_INVALIDDATA;
        goto Cleanup;
    }
    
    avst = avformat_new_stream(format_ctx, NULL);
    if (!avst)
    {
        ret = AVERROR(ENOMEM);
        goto Cleanup;
    }

    // use parse_full here - make the decoder 
    // do all the hard work about detection. We'll
    // just manage proper unpacking
    avst->nb_frames = 0;
    avst->need_parsing = AVSTREAM_PARSE_FULL;

    if (memcmp(&demux_ctx->file_header.majortype, &MEDIATYPE_Video, sizeof(GUID)) == 0)
    {
        avst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        avst->codecpar->codec_id = AV_CODEC_ID_H264;
    }
    else if (memcmp(&demux_ctx->file_header.majortype, &MEDIATYPE_Audio, sizeof(GUID)) == 0)
    {
        if (memcmp(&demux_ctx->file_header.formattype, &FORMAT_WaveFormatEx, sizeof(GUID)) != 0)
        {
            ret = AVERROR_INVALIDDATA;
            goto Cleanup;
        }

        wave_format = (WAVEFORMATEX*)demux_ctx->format_block;
        avst->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        avst->codecpar->codec_id = AV_CODEC_ID_AAC;
        avst->codecpar->codec_tag = 0;

        avst->codecpar->channels = wave_format->channels;
        avst->codecpar->channel_layout = wave_format->channels == 2? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
        avst->codecpar->sample_rate = wave_format->samplesPerSec;
        avst->codecpar->block_align = wave_format->blockAlign;
        avst->codecpar->frame_size = wave_format->bitsPerSample;
    }
    else
    {
        ret = AVERROR_INVALIDDATA;
        goto Cleanup;
    }    

Cleanup:
    return ret;
}

static int read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    PanrDemuxContext *demux_ctx = ctx->priv_data;
    PanrSampleHeader raw_header;
    int ret = 0;

    do
    {
        if (avio_feof(ctx->pb))
        {
            ret = AVERROR_EOF;
            goto Cleanup;
        }

        if (avio_read(ctx->pb, (uint8_t*) &raw_header, sizeof(raw_header)) != sizeof(raw_header))
        {
            ret = AVERROR_INVALIDDATA;
            goto Cleanup;
        }
        
        // make sure the header looks good
        if (raw_header.marker == raw_sample_signature)
        {
            if (raw_header.data_length <= demux_ctx->file_header.buffer_size)
            {
                break;
            }
        }

        // otherwise seek backwards and check the next byte
        avio_seek(ctx->pb, -sizeof(PanrSampleHeader) + 1, SEEK_CUR);
    } while (1);

    // we never have dts available
    pkt->dts = AV_NOPTS_VALUE;
    
    // extract only the absolute time for now
    // as the decoder seems to be just fine with that
    if (raw_header.time_relative)
    {
        pkt->pts = AV_NOPTS_VALUE;
        avio_seek(ctx->pb, sizeof(int32_t) * 2, SEEK_CUR);
    }
    else if (raw_header.time_absolute)
    {
        int64_t start_time = avio_rb64(ctx->pb);
        
        // read but ignore the end time - there was a bug in a
        // panr source that consistently corrupted this
        avio_rb64(ctx->pb);
        
        pkt->pts = start_time;
    }
    else
    {
        pkt->pts = AV_NOPTS_VALUE;
    }

    if (av_get_packet(ctx->pb, pkt, raw_header.data_length) != raw_header.data_length)
    {
        ret = AVERROR_INVALIDDATA;
        av_packet_unref(pkt);
        goto Cleanup;
    }

Cleanup:
    return ret;
}

static int read_close(AVFormatContext *ctx)
{
    PanrDemuxContext* demux_ctx = ctx->priv_data;
    
    if (demux_ctx->format_block)
    {
        av_free(demux_ctx->format_block);
        demux_ctx->format_block = NULL;
    }

    return 0;
}

AVInputFormat ff_panr_demuxer = {
    .name = "panr",
    .long_name = NULL_IF_CONFIG_SMALL("Panopto Raw File Parser"),
    .priv_data_size = sizeof(PanrDemuxContext),
    .read_probe = read_probe,
    .read_header = read_header,
    .read_packet = read_packet,
    .read_close = read_close
};
