/*
* Panr* Structure Definition
* Copyright (c) 2017 Dsessions
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
* Panr* Structure Definition
*/

#ifndef AVCODEC_PANR_H
#define AVCODEC_PANR_H

#define PANR_SIGNATURE ((uint32_t)'PANR')

// File header
//
// 4 bytes - File format signature "PANR" (PANopto Raw sample file format)
// 4 bytes - File format version
// 8 bytes - Start time of the initial sample (Optional. Negative value means invalid.)
// 8 bytes - End time of the last sample (Optional. Negative value means invalid.)
// 8 bytes - Wall clock time when the first sample arrives.
// 8 bytes - Wall clock time when the streaming stops. (Negative value means invalid, i.e .file was not finalized correctly.)
// 60 byte - Fields for media type (AM_MEDIA_TYPE.*. See definition for detail.)
// 4 bytes - Maximum buffer size
// 4 bytes - Size of format data section (N)
// N bytes - Format data (AM_MEDIA_TYPE.pbFormat)
typedef struct RawSampleFileHeader
{
    uint32_t    ffSignature;
    int32_t     ffVersion;
    int64_t     startTimeFirst;
    int64_t     endTimeLast;
    int64_t     startWallTime;
    int64_t     endWallTime;
    GUID        majortype;
    GUID        subtype;
    uint32_t    fFixedSizeSamples;
    uint32_t    fTemporalCompressions;
    uint32_t    lSampleSize;
    GUID        formattype;
    int32_t     bufferSize;
    uint32_t    nbformat;
} RawSampleFileHeader;

// Each sample entry
//
// 1 byte - Marker (0x9C) - this is for future seek support, without real indexing.
// 1 byte - 8 bit flags
// 4 bytes - length of data body (N)
// Sample time section
//    If fTimeAbsolute = 1, time information is 8 byte absoltue value. 
//    If fTimeRelative = 1, time information is 4 byte relative value from the previous sample.
//   0 or 4 or 8 bytes - start time
//   0 or 4 or 8 bytes - end time (DO NOT USE - INCONSISTENT)
// Media time section
//    If fMediaTimeAbsolute = 1, media time information is 8 byte absoltue value. 
//    If fMediaTimeRelative = 1, media time information is 4 byte relative value from the previous sample.
//   0 or 4 or 8 bytes - start media time
//   0 or 4 or 8 bytes - end media time (DO NOT USE - INCONSISTENT)
// N bytes - data body
static const uint8_t c_bRawSampleMarker = 0x9c;
typedef struct RawSampleHeader
{
    uint8_t marker;
    union
    {
        uint8_t bitFlags;
        struct
        {
            uint8_t discontinuity : 1;
            uint8_t preroll : 1;
            uint8_t syncPoint : 1;
            uint8_t timeAbsolute : 1;
            uint8_t timeRelative : 1;
            uint8_t mediaTimeAbsolute : 1;
            uint8_t mediaTimeRelative : 1;
            uint8_t reserved : 1;
        };
    };
    int32_t dataLength;
} RawSampleHeader;

#endif /* AVCODEC_PANR_H */
