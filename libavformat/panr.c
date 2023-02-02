/*
* Panopto Raw File (PANR)
*   *.panra = audio only
*   *.panrv = video only
*
* Copyright (c) 2023 Panopto
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
#include "avformat.h"
#include "libavcodec/put_bits.h"
#include "libavutil/channel_layout.h"
#include "libavcodec/mpeg4audio.h"
#include "internal.h"
#include "dshow.h"
#include "panr.h"


#define SAMPLE_INDEX_BUFFER_SIZE (512)

typedef struct SampleTimeEntry
{
    uint64_t file_pos;
    int64_t pts;
} SampleTimeEntry;

typedef struct PanrSampleIndex
{
    SampleTimeEntry samples[SAMPLE_INDEX_BUFFER_SIZE];
    int32_t next_open_idx;
    struct PanrSampleIndex* next;
} PanrSampleIndex;

typedef struct PanrDemuxContext
{
    uint8_t first_sample;
    PanrSampleFileHeader file_header;
    uint8_t* format_block;
    PanrSampleIndex* sample_index;
    int64_t last_sample_pos;

    // audio specific data
    uint32_t audio_object_type;
    uint32_t audio_sampling_index;
    uint32_t audio_channel_config;
} PanrDemuxContext;

static int read_probe(const AVProbeData *probe_data)
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
    avst->avg_frame_rate.num = 1000;
    if (memcmp(format_type, &PANR_FORMAT_VideoInfo, sizeof(GUID)) == 0)
    {
        PANR_VIDEOINFOHEADER* vih = (PANR_VIDEOINFOHEADER*)format_block;
        avst->avg_frame_rate.den = vih->avgTimePerFrame / 10000;
        avst->codecpar->bit_rate = vih->bitRate;
    }
    else if (memcmp(format_type, &PANR_FORMAT_VideoInfo2, sizeof(GUID)) == 0)
    {
        PANR_VIDEOINFOHEADER2* vih = (PANR_VIDEOINFOHEADER2*)format_block;
        avst->avg_frame_rate.den = vih->avgTimePerFrame / 10000;
        avst->codecpar->bit_rate = vih->dwBitRate;
    }
    else if (memcmp(format_type, &PANR_FORMAT_MPEGVideo, sizeof(GUID)) == 0)
    {
        PANR_MPEG1VIDEOINFO* vih = (PANR_MPEG1VIDEOINFO*)format_block;
        avst->avg_frame_rate.den = vih->hdr.avgTimePerFrame / 10000;
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
        avst->avg_frame_rate.den = vih->hdr.avgTimePerFrame / 10000;
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

    // ensure we're in a known good state
    // read_header is the init function for our demux context
    demux_ctx->sample_index = NULL;
    demux_ctx->format_block = NULL;
    demux_ctx->first_sample = 1;

    if (avio_read(pBuffer, (uint8_t*) &demux_ctx->file_header, sizeof(PanrSampleFileHeader))
            != sizeof(PanrSampleFileHeader))
    {
        ret = AVERROR_INVALIDDATA;
        av_log(format_ctx, AV_LOG_ERROR, "Failed to read the PanrSampleFileHeader due to insufficient data\n");
        goto Cleanup;
    }

    demux_ctx->format_block = av_malloc(demux_ctx->file_header.cb_format);
    if (!demux_ctx->format_block)
    {
        ret = AVERROR(ENOMEM);
        av_log(format_ctx, AV_LOG_ERROR, "Failed allocate the header format block memory\n");
        goto Cleanup;
    }

    if (avio_read(pBuffer, (uint8_t*) demux_ctx->format_block, demux_ctx->file_header.cb_format)
            != demux_ctx->file_header.cb_format)
    {
        ret = AVERROR_INVALIDDATA;
        av_log(format_ctx, AV_LOG_ERROR, "Failed to read  header format block memory from the file due to insufficent data\n");
        goto Cleanup;
    }

    avst = avformat_new_stream(format_ctx, NULL);
    if (!avst)
    {
        ret = AVERROR(ENOMEM);
        av_log(format_ctx, AV_LOG_ERROR, "Failed to allocate a new stream\n");
        goto Cleanup;
    }

    // set the pts info to match dshow timestamps
    avpriv_set_pts_info(avst, 64, 1, 10000000);

    // use parse_full here - make the decoder
    // do all the hard work about detection. We'll
    // just manage proper unpacking
    avst->nb_frames = 0;

    // For ffmpeg 5.1.2 build, we noticed that this field does not exist anymore.
    // avst->need_parsing = AVSTREAM_PARSE_NONE;

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
            av_log(format_ctx, AV_LOG_DEBUG, "extract_video_format_block_info returned a non-zero error code %d\n", ret);
            goto Cleanup;
        }

        // for some reason sometimes this is in kilobits
        // and other times in bits...just apply a heuristic
        // to try to get it right
        if (avst->codecpar->bit_rate < 20000)
        {
            av_log(format_ctx, AV_LOG_TRACE, "Parsed bitrate was too low, multiplying up by 1000\n");
            avst->codecpar->bit_rate *= 1000;
        }
    }
    else if (memcmp(&demux_ctx->file_header.majortype, &PANR_MEDIATYPE_Audio, sizeof(GUID)) == 0)
    {
        if (memcmp(&demux_ctx->file_header.formattype, &PANR_FORMAT_WaveFormatEx, sizeof(GUID)) != 0)
        {
            ret = AVERROR_INVALIDDATA;
            av_log(format_ctx, AV_LOG_ERROR, "Detected audio format header type was not WaveFormatEx, and is thus not supported \n");
            goto Cleanup;
        }

        wave_format = (WAVEFORMATEX*)demux_ctx->format_block;
        avst->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        avst->codecpar->codec_id = AV_CODEC_ID_AAC;
        avst->codecpar->codec_tag = MKTAG('m', 'p', '4', 'a');

        avst->codecpar->profile = FF_PROFILE_AAC_LOW;
        avst->codecpar->channels = wave_format->nChannels;
        avst->codecpar->channel_layout = wave_format->nChannels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
        avst->codecpar->sample_rate = wave_format->nSamplesPerSec;
        avst->codecpar->block_align = wave_format->nBlockAlign;

        // prepare some data to emit in the esds audio specific config via side_data
        // see https://wiki.multimedia.cx/index.php?title=MPEG-4_Audio
        // (more context: https://stackoverflow.com/questions/3987850/mp4-atom-how-to-discriminate-the-audio-codec-is-it-aac-or-mp3)
        demux_ctx->audio_object_type = AOT_AAC_LC;
        demux_ctx->audio_sampling_index = UINT_MAX;
        for (int i = 0; i < sizeof(ff_mpeg4audio_sample_rates) / sizeof(ff_mpeg4audio_sample_rates[0]); i++)
        {
            if (avst->codecpar->sample_rate == ff_mpeg4audio_sample_rates[i])
            {
                demux_ctx->audio_sampling_index = i;
                break;
            }
        }

        if (demux_ctx->audio_sampling_index == UINT_MAX)
        {
            // default to 44.1 if we don't find a match
            demux_ctx->audio_sampling_index = 4;
            av_log(format_ctx,
                AV_LOG_WARNING,
                "Could not find a sample rate index match for sample rate %d, defaulting to 44100\n",
                avst->codecpar->sample_rate);
        }

        demux_ctx->audio_channel_config = wave_format->nChannels > 0 ? wave_format->nChannels : 1;
    }
    else
    {
        ret = AVERROR_INVALIDDATA;
        av_log(format_ctx, AV_LOG_ERROR, "Unrecognized major type - unable to parse this data\n");
        goto Cleanup;
    }

    demux_ctx->sample_index = (PanrSampleIndex*) av_malloc(sizeof(PanrSampleIndex));
    if (!demux_ctx->sample_index)
    {
        ret = AVERROR(ENOMEM);
        av_log(format_ctx, AV_LOG_ERROR, "Failed to allocate a PanrSampleIndex in read_header\n");
        goto Cleanup;
    }
    demux_ctx->sample_index->next = NULL;
    demux_ctx->sample_index->next_open_idx = 0;

Cleanup:
    return ret;
}

static int read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    PanrDemuxContext *demux_ctx = ctx->priv_data;
    PanrSampleHeader raw_header;
    int ret = 0, buffer_read_size;
    int64_t marker_pos;
    int64_t pkt_pts;
    int started_data_gap_scan = 0;

    do
    {
        if (avio_feof(ctx->pb))
        {
            ret = AVERROR_EOF;
            av_log(ctx, AV_LOG_TRACE, "End of file encountered\n");
            goto Cleanup;
        }

        marker_pos = avio_tell(ctx->pb);
        buffer_read_size = avio_read(ctx->pb, (uint8_t*) &raw_header, sizeof(raw_header));
        if (buffer_read_size < sizeof(raw_header))
        {
            ret = AVERROR_EOF;
            av_log(ctx, AV_LOG_INFO, "End of file encountered while trying to read the raw header size, ending parsing. Read %d bytes\n", buffer_read_size);
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

        if (started_data_gap_scan == 0)
        {
            av_log(ctx,
                AV_LOG_WARNING,
                "Failed to detect the next sample immediately, scanning forawrd in the file to find the next sample header. Position: %" PRId64 " \n", marker_pos);
        }

        // otherwise seek backwards and check the next byte
        avio_seek(ctx->pb, -sizeof(PanrSampleHeader) + 1, SEEK_CUR);
        started_data_gap_scan = 1;
    } while (1);

    // for now we fully rely on the downstream components
    // to extract the pts from the actual samples. This block
    // is left here in case we need to change to actually properly
    // handle them as per the panr* format
    if (raw_header.time_relative)
    {
        PanrSampleIndex* cur_idx = demux_ctx->sample_index;
        int start_delta = avio_rl32(ctx->pb);
        int64_t last_pts = 0;

        // find the last timestamp before this one
        while (cur_idx)
        {
            // if we have nothing in this packet use the last cached or
            // if the first sample is too far
            // for simplicity if we find a match we
            // consider it too far and keep moving
            if (cur_idx->next_open_idx == 0 ||
                 cur_idx->samples[0].file_pos >= marker_pos)
            {
                break;
            }

            // if we're outside the scope of what's in this buffer
            if (cur_idx->samples[cur_idx->next_open_idx - 1].file_pos < marker_pos)
            {
                // no matter what we'll want to cache the last pts - either
                // its the target or we need it in case the next block is empty
                last_pts = cur_idx->samples[cur_idx->next_open_idx - 1].pts;

                // if the buffer was already full cache the last pts and move
                // on to the next buffer
                if (cur_idx->next_open_idx >= SAMPLE_INDEX_BUFFER_SIZE)
                {
                    cur_idx = cur_idx->next;
                    continue;
                }
                else
                {
                    // found the best match - last one in here
                    break;
                }
            }

            // alright we know the answer is in this buffer somewhere...track it down!
            // always start from the back since in general we expect constant forward seeks
            // we can get away with open_idx - 2 here because the fall apart case is when
            // open_idx = 1, which means there's only one sample, and we'll have cached
            // the right value already above
            for (int i = cur_idx->next_open_idx - 2; i >= 0; i--)
            {
                last_pts = cur_idx->samples[i].pts;
                if (cur_idx->samples[i].file_pos < marker_pos)
                {
                    break;
                }
            }

            break;
        }

        pkt_pts = last_pts + start_delta;

        // read AND ALWAYS ignore the end time - there was a bug in a
        // panr source that consistently corrupted this
        avio_rl32(ctx->pb);
    }
    else if (raw_header.time_absolute)
    {
        pkt_pts  = avio_rl64(ctx->pb);

        // read AND ALWAYS ignore the end time - there was a bug in a
        // panr source that consistently corrupted this
        avio_rl64(ctx->pb);
    }
    else
    {
        pkt_pts = AV_NOPTS_VALUE;
    }

    if (raw_header.media_time_absolute)
    {
        avio_rl64(ctx->pb);
        avio_rl64(ctx->pb);
    }
    else if (raw_header.media_time_relative)
    {
        avio_rl64(ctx->pb);
    }

    if (av_get_packet(ctx->pb, pkt, raw_header.data_length) != raw_header.data_length)
    {
        ret = AVERROR_INVALIDDATA;
        av_log(ctx, AV_LOG_WARNING, "Failed to read the packet at byte %" PRIu64 " due to an end of file being reached\n", marker_pos);
        av_packet_unref(pkt);
        goto Cleanup;
    }
    pkt->pts = pkt_pts;
    // if we have an unknown duration set it here to 0
    pkt->duration = 0;

    if (raw_header.syncpoint)
    {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    // record the pts for this sample position if it's a new max
    // there's no way to seek forward besides this so we're guaranteed
    // to be able to make some clever decisions here
    if (marker_pos > demux_ctx->last_sample_pos)
    {
        PanrSampleIndex* cur_idx = demux_ctx->sample_index;
        while (cur_idx->next)
        {
            cur_idx = cur_idx->next;
        }

        if (cur_idx->next_open_idx >= SAMPLE_INDEX_BUFFER_SIZE)
        {
            cur_idx->next = (PanrSampleIndex*) av_malloc(sizeof(PanrSampleIndex));
            if (!cur_idx->next )
            {
                ret = AVERROR(ENOMEM);
                av_log(ctx, AV_LOG_ERROR, "Failed to allocate a PanrSampleIndex when increasing the sample buffer size\n");
                goto Cleanup;
            }
            cur_idx = cur_idx->next;
            cur_idx->next = NULL;
            cur_idx->next_open_idx = 0;
        }

        cur_idx->samples[cur_idx->next_open_idx].file_pos = marker_pos;
        cur_idx->samples[cur_idx->next_open_idx].pts = pkt->pts;
        cur_idx->next_open_idx++;

        demux_ctx->last_sample_pos = marker_pos;
    }

    if (pkt->pts != AV_NOPTS_VALUE)
    {
        av_add_index_entry(ctx->streams[0],
                            marker_pos,
                            pkt->pts,
                            avio_tell(ctx->pb) - marker_pos,
                            0,  // distance
                            raw_header.syncpoint? AVINDEX_KEYFRAME : 0);
    }
    else
    {
        av_log(ctx, AV_LOG_INFO, "Sample at %" PRIu64 " has no detected pts\n", marker_pos);
    }

    if (demux_ctx->first_sample && memcmp(&demux_ctx->file_header.majortype, &PANR_MEDIATYPE_Audio, sizeof(GUID)) == 0)
    {
        PutBitContext put_bit_ctx;
        uint8_t *side_data;
        av_log(ctx, AV_LOG_DEBUG, "Emitting audio specific extradata for the first audio sample\n");

        side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, 2);
        if (!side_data)
        {
            ret = AVERROR(ENOMEM);
            av_log(ctx, AV_LOG_ERROR, "Failed to allocate a packet side data\n");
            goto Cleanup;
        }

        init_put_bits(&put_bit_ctx, side_data, 2);
        put_bits(&put_bit_ctx, 5, demux_ctx->audio_object_type); // object_type
        put_bits(&put_bit_ctx, 4, demux_ctx->audio_sampling_index); // sampling_index
        put_bits(&put_bit_ctx, 4, demux_ctx->audio_channel_config); // chan_config
        put_bits(&put_bit_ctx, 1, 0); //frame length - 1024 samples
        put_bits(&put_bit_ctx, 1, 0); //does not depend on core coder
        put_bits(&put_bit_ctx, 1, 0); //is not extension
        flush_put_bits(&put_bit_ctx);

        demux_ctx->first_sample = 0;
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
    .read_close = read_close,
    .flags = AVFMT_GLOBALHEADER | AVFMT_GENERIC_INDEX | AVFMT_TS_DISCONT
};
