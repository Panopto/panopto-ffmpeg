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

static int extract_video_format_block_info(GUID* format_type, int8_t* format_block, AVStream* avst)
{
    int ret = 0;
    
    // numerator should always be 1 second in 100 ns ticks to match dshow behavior
    avst->avg_frame_rate.num = 10000000;
    if (memcmp(format_type, &PANR_FORMAT_VideoInfo, sizeof(GUID)) == 0)
    {
        PANR_VIDEOINFOHEADER* vih = (PANR_VIDEOINFOHEADER*)format_block;
        avst->avg_frame_rate.den = vih->avgTimePerFrame;
        avst->codecpar->bit_rate = vih->bitRate;
    }
    else if (memcmp(format_type, &PANR_FORMAT_VideoInfo2, sizeof(GUID)) == 0)
    {
        PANR_VIDEOINFOHEADER2* vih = (PANR_VIDEOINFOHEADER2*)format_block;
        avst->avg_frame_rate.den = vih->avgTimePerFrame;
        avst->codecpar->bit_rate = vih->dwBitRate;
    }
    else if (memcmp(format_type, &PANR_FORMAT_MPEGVideo, sizeof(GUID)) == 0)
    {
        PANR_MPEG1VIDEOINFO* vih = (PANR_MPEG1VIDEOINFO*)format_block;
        avst->avg_frame_rate.den = vih->hdr.avgTimePerFrame;
        avst->codecpar->bit_rate = vih->hdr.bitRate;
    }
    else if (memcmp(format_type, &PANR_FORMAT_MPEGStreams, sizeof(GUID)) == 0)
    {
        PANR_AM_MPEGSYSTEMTYPE* vih = (PANR_AM_MPEGSYSTEMTYPE*)format_block;
        if (vih->cStreams < 1)
        {
            ret = AVERROR_INVALIDDATA;
            goto Cleanup;
        }
    
        avst->codecpar->bit_rate = vih->dwBitRate;
        ret = extract_video_format_block_info(
            &vih->Streams[0].mt.formattype,
            vih->Streams[0].bFormat,
            avst);
    }
    else if (memcmp(format_type, &PANR_FORMAT_MPEG2Video, sizeof(GUID)) == 0)
    {
        PANR_MPEG2VIDEOINFO* vih = (PANR_MPEG2VIDEOINFO*)format_block;
        avst->codecpar->bit_rate = vih->hdr.dwBitRate;
        avst->avg_frame_rate.den = vih->hdr.avgTimePerFrame;
    }
    else
    {
        ret = AVERROR_INVALIDDATA;
        goto Cleanup;
    }

Cleanup:
    return ret;
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
    avst->need_parsing = AVSTREAM_PARSE_FULL_RAW;

    if (memcmp(&demux_ctx->file_header.majortype, &PANR_MEDIATYPE_Video, sizeof(GUID)) == 0)
    {
        avst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        avst->codecpar->codec_id = AV_CODEC_ID_H264;
        
        ret = extract_video_format_block_info(
                        &demux_ctx->file_header.formattype, 
                        demux_ctx->format_block, 
                        avst);
                        
        if (ret != 0)
        {
            goto Cleanup;
        }
        
        // for some reason sometimes this is in kilobits
        // and other times in bits...just apply a heuristic
        // to try to get it right
        if (avst->codecpar->bit_rate < 20000)
        {
                avst->codecpar->bit_rate *= 1000;
        }
    }
    else if (memcmp(&demux_ctx->file_header.majortype, &PANR_MEDIATYPE_Audio, sizeof(GUID)) == 0)
    {
        if (memcmp(&demux_ctx->file_header.formattype, &PANR_FORMAT_WaveFormatEx, sizeof(GUID)) != 0)
        {
            ret = AVERROR_INVALIDDATA;
            goto Cleanup;
        }

        wave_format = (WAVEFORMATEX*)demux_ctx->format_block;
        avst->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        avst->codecpar->codec_id = AV_CODEC_ID_AAC;
        avst->codecpar->codec_tag = 0;

        avst->codecpar->channels = wave_format->nChannels;
        avst->codecpar->channel_layout = wave_format->nChannels == 2? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
        avst->codecpar->sample_rate = wave_format->nSamplesPerSec;
        avst->codecpar->block_align = wave_format->nBlockAlign;
        avst->codecpar->frame_size = wave_format->wBitsPerSample;
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
    int ret = 0, buffer_read_size;

    do
    {
        if (avio_feof(ctx->pb))
        {
            ret = AVERROR_EOF;
            goto Cleanup;
        }

        buffer_read_size = avio_read(ctx->pb, (uint8_t*) &raw_header, sizeof(raw_header));
        if ( buffer_read_size < sizeof(raw_header))
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
    
    // for now we fully rely on the downstream components
    // to extract the pts from the actual samples. This block
    // is left here in case we need to change to actually properly
    // handle them as per the panr* format
    if (raw_header.time_relative)
    {
        pkt->pts = AV_NOPTS_VALUE;
        avio_seek(ctx->pb, sizeof(int32_t) * 2, SEEK_CUR);
    }
    else if (raw_header.time_absolute)
    {
        // read start time - for now we rely on the 
        // decoder to handle actually extracting the
        // proper pts from the samples rather than
        // relying on the dshow times
        avio_rb64(ctx->pb);
        
        // read AND ALWAYS ignore the end time - there was a bug in a
        // panr source that consistently corrupted this
        avio_rb64(ctx->pb);
        
        pkt->pts = AV_NOPTS_VALUE;
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
