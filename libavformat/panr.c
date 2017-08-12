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
    void* formatBlock;
} PanrDemuxContext;

static const LONG c_cbMaxRawSampleHeader = sizeof(RawSampleHeader) + sizeof(LONGLONG) * 4;

static int read_probe(AVProbeData *p)
{
    if (p->buf_size >= sizeof(RawSampleFileHeader) &&
        !memcmp(p->buf, PANR_SIGNATURE, strlen(PANR_SIGNATURE)))
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

static int read_header(AVFormatContext * pFormatContext)
{
    PanrDemuxContext *pDemuxContext = pFormatContext->priv_data;
    AVIOContext     *pBuffer = pFormatContext->pb;
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
    
    if (avio_read(pBuffer, &pDemuxContext->formatBlock, pDemuxContext->fileHeader.nbformat)
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

        // for now don't bother with the rest if we don't need to...
    }
    else if (memcmp(&pDemuxContext->fileHeader.majortype, &MEDIATYPE_Audio, sizeof(GUID) == 0))
    {
        if (memcmp(&pDemuxContext->fileHeader.formattype, &FORMAT_WaveFormatEx, sizeof(GUID) != 0))
        {
            ret = AVERROR_INVALIDDATA;
            goto Cleanup;
        }

        WAVEFORMATEX* pWaveFormat = (WAVEFORMATEX*)pDemuxContext->formatBlock;
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
    uint32_t        count, offset;
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

AVInputFormat ff_paf_demuxer = {
    .name = "panr",
    .long_name = NULL_IF_CONFIG_SMALL("Panopto Raw File Parser"),
    .priv_data_size = sizeof(PanrDemuxContext),
    .read_probe = read_probe,
    .read_header = read_header,
    .read_packet = read_packet,
    .read_close = read_close
};
