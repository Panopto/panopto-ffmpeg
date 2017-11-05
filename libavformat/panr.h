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

// Match windows packing
#pragma pack(push, 1)

// Resolves to 'PANR'
// Necessary to avoid warnings/portability issues
static const uint32_t panr_signature = 1346457170;

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
typedef struct PanrSampleFileHeader
{
    uint32_t    signature;
    int32_t      version;
    int64_t      start_time_first;
    int64_t      end_time_last;
    int64_t      start_wall_time;
    int64_t      end_wall_time;
    GUID         majortype;
    GUID         subtype;
    uint32_t    fixed_size_samples;
    uint32_t    temporal_compression;
    uint32_t    sample_size;
    GUID         formattype;
    int32_t      buffer_size;
    uint32_t    cb_format;
} __attribute__((ms_struct)) PanrSampleFileHeader;

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
static const uint8_t raw_sample_signature = 0x9c;
typedef struct PanrSampleHeader
{
    uint8_t marker;
    union
    {
        uint8_t bit_flags;
        struct
        {
            uint8_t discontinuity : 1;
            uint8_t preroll : 1;
            uint8_t syncpoint : 1;
            uint8_t time_absolute : 1;
            uint8_t time_relative : 1;
            uint8_t media_time_absolute : 1;
            uint8_t media_time_relative: 1;
            uint8_t reserved : 1;
        } __attribute__((ms_struct));
    };
    int32_t data_length;
} __attribute__((ms_struct)) PanrSampleHeader;

#pragma pack(pop)

#endif /* AVCODEC_PANR_H */
