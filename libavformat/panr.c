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
    RawSampleFileHeader fileHeader;
    uint8_t* formatBlock;
} PanrDemuxContext;

static const int32_t c_cbMaxRawSampleHeader = sizeof(RawSampleHeader) + sizeof(int64_t) * 4;

static int read_probe(AVProbeData *p)
{
    if (p->buf_size >= sizeof(RawSampleFileHeader) &&
        ((uint32_t*)p->buf)[0] == sc_panrSignature)
    {
        RawSampleFileHeader* pTestHeader = (RawSampleFileHeader*) p->buf;
        // only V1 is supported
        if (pTestHeader->ffVersion == 1)
        {
            return AVPROBE_SCORE_MAX;
        }
    }

    return 0;
}

static int get_width_and_height_from_format(GUID* format_type, int8_t* format_block, int* outWidth, int* outHeight)
{
    int ret = 0;
    if (memcmp(format_type, &FORMAT_VideoInfo, sizeof(GUID) == 0))
    {
        VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)format_block;
        *outWidth = vih->bmiHeader.biWidth;
        *outHeight = vih->bmiHeader.biHeight;
    }
    else if (memcmp(format_type, &FORMAT_VideoInfo2, sizeof(GUID) == 0))
    {
        VIDEOINFOHEADER2* vih = (VIDEOINFOHEADER2*)format_block;
        *outWidth = vih->bmiHeader.biWidth;
        *outHeight = vih->bmiHeader.biHeight;
    }
    else if (memcmp(format_type, &FORMAT_MPEGVideo, sizeof(GUID) == 0))
    {
        MPEG1VIDEOINFO* vih = (MPEG1VIDEOINFO*)format_block;
        *outWidth = vih->hdr.bmiHeader.biWidth;
        *outHeight = vih->hdr.bmiHeader.biHeight;
    }
    else if (memcmp(format_type, &FORMAT_MPEGStreams, sizeof(GUID) == 0))
    {
        AM_MPEGSYSTEMTYPE* vih = (AM_MPEGSYSTEMTYPE*)format_block;
        if (vih->cStreams < 1)
        {
            ret = AVERROR_INVALIDDATA;
            goto Cleanup;
        }

        ret = get_width_and_height_from_format(
            &vih->Streams[0].mt.formattype,
            vih->Streams[0].bFormat,
            outWidth,
            outHeight);
    }
    else if (memcmp(format_type, &FORMAT_MPEG2Video, sizeof(GUID) == 0))
    {
        MPEG2VIDEOINFO* vih = (MPEG2VIDEOINFO*)format_block;
        *outWidth = vih->hdr.bmiHeader.biWidth;
        *outHeight = vih->hdr.bmiHeader.biHeight;
    }
    else
    {
        ret = AVERROR_INVALIDDATA;
        goto Cleanup;
    }

Cleanup:
    return ret;
}

static int read_header(AVFormatContext * pFormatContext)
{
    PanrDemuxContext *pDemuxContext = pFormatContext->priv_data;
    AVIOContext     *pBuffer = pFormatContext->pb;
    WAVEFORMATEX    *pWaveFormat;
    AVStream        *avst = NULL;
    int ret = 0;

    if (avio_read(pBuffer, &pDemuxContext->fileHeader, sizeof(RawSampleFileHeader))
        != sizeof(RawSampleFileHeader))
    {
        ret = AVERROR_INVALIDDATA;
        goto Cleanup;
    }

    pDemuxContext->formatBlock = av_malloc(pDemuxContext->fileHeader.nbformat);
    if (!pDemuxContext->formatBlock)
    {
        ret = AVERROR(ENOMEM);
        goto Cleanup;
    }
    
    if (avio_read(pBuffer, pDemuxContext->formatBlock, pDemuxContext->fileHeader.nbformat)
            != pDemuxContext->fileHeader.nbformat)
    {
        ret = AVERROR_INVALIDDATA;
        goto Cleanup;
    }
    
    avst = avformat_new_stream(pFormatContext, NULL);
    if (!avst)
    {
        ret = AVERROR(ENOMEM);
        goto Cleanup;
    }
    
    if (memcmp(&pDemuxContext->fileHeader.majortype, &MEDIATYPE_Video, sizeof(GUID) == 0))
    {
        avst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        avst->codecpar->codec_id = AV_CODEC_ID_H264;

        if (get_width_and_height_from_format(&pDemuxContext->fileHeader.formattype,
                                pDemuxContext->formatBlock,
                                &avst->codecpar->width,
                                &avst->codecpar->height) != 0)
        {
            ret = AVERROR_INVALIDDATA;
            goto Cleanup;
        }
        
        // for now don't bother with the rest if we don't need to...
    }
    else if (memcmp(&pDemuxContext->fileHeader.majortype, &MEDIATYPE_Audio, sizeof(GUID) == 0))
    {
        if (memcmp(&pDemuxContext->fileHeader.formattype, &FORMAT_WaveFormatEx, sizeof(GUID) != 0))
        {
            ret = AVERROR_INVALIDDATA;
            goto Cleanup;
        }

        pWaveFormat = (WAVEFORMATEX*)pDemuxContext->formatBlock;
        avst->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        avst->codecpar->codec_id = AV_CODEC_ID_AAC;
        avst->codecpar->codec_tag = 0;

        avst->codecpar->channels = pWaveFormat->channels;
        avst->codecpar->channel_layout = pWaveFormat->channels == 2? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
        avst->codecpar->sample_rate = pWaveFormat->samplesPerSec;
        avst->codecpar->block_align = pWaveFormat->blockAlign;
        avst->codecpar->frame_size = pWaveFormat->bitsPerSample;
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
    PanrDemuxContext *pDemuxContext = ctx->priv_data;
    int             ret = 0;
    RawSampleHeader rawHeader;

    do
    {
        if (avio_feof(ctx->pb))
        {
            ret = AVERROR_EOF;
            goto Cleanup;
        }

        if (avio_read(ctx->pb, &rawHeader, sizeof(rawHeader)) != sizeof(RawSampleHeader))
        {
            ret = AVERROR_INVALIDDATA;
            goto Cleanup;
        }
        
        // make sure the header looks good
        if (rawHeader.marker == c_bRawSampleMarker)
        {
            if (rawHeader.dataLength <= pDemuxContext->fileHeader.bufferSize)
            {
                break;
            }
        }

        // otherwise seek backwards and check the next byte
        avio_seek(ctx->pb, -sizeof(RawSampleHeader) + 1, SEEK_CUR);
    } while (1);

    if (av_get_packet(ctx->pb, pkt, rawHeader.dataLength) != rawHeader.dataLength)
    {
        ret = AVERROR_INVALIDDATA;
        goto Cleanup;
    }

Cleanup:
    return ret;
}

static int read_close(AVFormatContext *ctx)
{
    PanrDemuxContext* pDemuxContext = ctx->priv_data;
    
    if (pDemuxContext->formatBlock)
    {
        av_free(pDemuxContext->formatBlock);
        pDemuxContext->formatBlock = NULL;
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
