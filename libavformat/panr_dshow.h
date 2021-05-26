/*
* DShow Structure Definition Copies
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

/**
* @file
* DShow Structure Definition Copies
*/

#ifndef AVCODEC_DSHOW_H
#define AVCODEC_DSHOW_H

// Match windows packing
#pragma pack(push, 1)

#ifndef WIN32

// Matched to guiddef.h from Windows
typedef struct GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} __attribute__((ms_struct)) GUID;

// Type redefs
typedef struct RECT
{
    int32_t    left;
    int32_t    top;
    int32_t    right;
    int32_t    bottom;
} __attribute__((ms_struct)) RECT;

typedef struct BITMAPINFOHEADER {
    int32_t      biSize;
    int32_t      biWidth;
    int32_t      biHeight;
    int16_t      biPlanes;
    int16_t      biBitCount;
    int32_t      biCompression;
    int32_t      biSizeImage;
    int32_t      biXPelsPerMeter;
    int32_t      biYPelsPerMeter;
    int32_t      biClrUsed;
    int32_t      biClrImportant;
} __attribute__((ms_struct)) BITMAPINFOHEADER;

typedef struct WAVEFORMATEX
{
    uint16_t    wFormatTag;
    uint16_t    nChannels;
    uint32_t    nSamplesPerSec;
    uint32_t    nAvgBytesPerSec;
    uint16_t    nBlockAlign;
    uint16_t    wBitsPerSample;
    uint16_t    cbSize;
} __attribute__((ms_struct)) WAVEFORMATEX;

#endif /* WIN32 */

// Import necessary GUIDs
const GUID PANR_MEDIATYPE_Video = { 0x73646976, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
const GUID PANR_MEDIATYPE_Audio = { 0x73647561, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

const GUID PANR_MEDIASUBTYPE_H264_ALTERNATE = { 0x8d2d71cb, 0x243f, 0x45e3,{ 0xb2, 0xd8, 0x5f, 0xd7, 0x96, 0x7e, 0xc0, 0x9b } };
const GUID PANR_MEDIASUBTYPE_H264 = { 0x34363248, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
const GUID PANR_FORMAT_WaveFormatEx = { 0x05589f81, 0xc356, 0x11ce,{ 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };

const GUID PANR_FORMAT_VideoInfo = { 0x05589f80, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
const GUID PANR_FORMAT_VideoInfo2 = { 0xf72a76A0, 0xeb0a, 0x11d0, { 0xac, 0xe4, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };

const GUID PANR_FORMAT_MPEGVideo = { 0x05589f82, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
const GUID PANR_FORMAT_MPEGStreams = { 0x05589f83, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
const GUID PANR_FORMAT_MPEG2Video = { 0xe06d80e3, 0xdb46, 0x11cf, { 0xb4, 0xd1, 0x00, 0x80, 0x05f, 0x6c, 0xbb, 0xea } };

typedef struct PANR_VIDEOINFOHEADER {

    RECT            rcSource;
    RECT            rcTarget;
    int32_t         bitRate;
    int32_t         bitErrorRate;
    int64_t         avgTimePerFrame;
    BITMAPINFOHEADER bmiHeader;
} __attribute__((ms_struct)) PANR_VIDEOINFOHEADER;

typedef struct PANR_VIDEOINFOHEADER2 {
    RECT        rcSource;
    RECT        rcTarget;
    int32_t     dwBitRate;
    int32_t     dwBitErrorRate;
    int64_t     avgTimePerFrame;
    int32_t     dwInterlaceFlags;
    int32_t     dwCopyProtectFlags;
    int32_t     dwPictAspectRatioX;
    int32_t     dwPictAspectRatioY;
    int32_t     dwReserved1;
    int32_t     dwReserved2;
    BITMAPINFOHEADER    bmiHeader;
} __attribute__((ms_struct)) PANR_VIDEOINFOHEADER2;

typedef struct PANR_MPEG1VIDEOINFO {

    PANR_VIDEOINFOHEADER hdr;
    int32_t           dwStartTimeCode;
    int32_t           cbSequenceHeader;
    int8_t            bSequenceHeader[1];
} __attribute__((ms_struct)) PANR_MPEG1VIDEOINFO;

typedef struct PANR_MPEG2VIDEOINFO {
    PANR_VIDEOINFOHEADER2    hdr;
    int32_t               dwStartTimeCode;
    int32_t               cbSequenceHeader;
    int32_t               dwProfile;
    int32_t               dwLevel;
    int32_t               dwFlags;
    int32_t               dwSequenceHeader[1];
} __attribute__((ms_struct)) PANR_MPEG2VIDEOINFO;

typedef struct PANR_AMMediaType
{
    GUID majortype;
    GUID subtype;
    int32_t bFixedSizeSamples;
    int32_t bTemporalCompression;
    uint32_t lSampleSize;
    GUID formattype;
    void *pUnk;
    uint32_t cbFormat;
    int8_t *pbFormat;
} __attribute__((ms_struct)) PANR_AM_MEDIA_TYPE;

typedef struct PANR_AM_MPEGSTREAMTYPE
{
    int32_t             dwStreamId; 
    int32_t             dwReserved; 
    PANR_AM_MEDIA_TYPE     mt; 
    int8_t              bFormat[1];
} __attribute__((ms_struct)) PANR_AM_MPEGSTREAMTYPE;

typedef struct PANR_AM_MPEGSYSTEMTYPE
{
    int32_t             dwBitRate;
    int32_t             cStreams;
    PANR_AM_MPEGSTREAMTYPE Streams[1];
} __attribute__((ms_struct)) PANR_AM_MPEGSYSTEMTYPE;

#pragma pack(pop)

#endif /* AVCODEC_DSHOW_H */
