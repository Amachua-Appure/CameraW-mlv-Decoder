/*
 * Copyright (C) 2016 Magic Lantern Team
 *
 * This file is part of Magic Lantern. Whereas Magic Lantern itself
 * is distributed under GPL license, this special header file is meant 
 * as a file format specification and thus distributed under a compatible,
 * more flexible license to achieve maximum file format compatibility.
 *
 ***********************************************************************
 *        WARNING: The LGPL license only applies to this one file      *
 ***********************************************************************
 *
 * This header file is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This header is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MLV_STRUCTURE_H_
#define _MLV_STRUCTURE_H_

#define MLV_VERSION_STRING "v2.0"
#define MLV_VIDEO_CLASS_RAW          0x01
#define MLV_VIDEO_CLASS_YUV          0x02
#define MLV_VIDEO_CLASS_JPEG         0x03
#define MLV_VIDEO_CLASS_H264         0x04

#define MLV_VIDEO_CLASS_FLAG_LZMA    0x80
#define MLV_VIDEO_CLASS_FLAG_DELTA   0x40
#define MLV_VIDEO_CLASS_FLAG_LJ92    0x20

#define MLV_AUDIO_CLASS_FLAG_LZMA    0x80

#define MLV_FRAME_UNSPECIFIED 0
#define MLV_FRAME_VIDF        1
#define MLV_FRAME_AUDF        2


#pragma pack(push,1)

struct raw_info
{
    uint32_t api_version;
#if INTPTR_MAX == INT32_MAX
    void* buffer;
#else
    uint32_t do_not_use_this;
#endif

    uint32_t height, width, pitch;
    uint32_t frame_size;
    uint32_t bits_per_pixel; 

    uint32_t black_level; 
    uint32_t white_level; 
  
    union
    {
        struct
        {
            uint32_t x, y;
            uint32_t width, height;
        } jpeg;
        struct
        {
            uint32_t origin[2];
            uint32_t size[2];
        } crop;
    };
    union
    {
        struct
        {
            uint32_t y1, x1, y2, x2;
        } active_area;
        uint32_t dng_active_area[4];
    };
    uint32_t exposure_bias[2];
    uint32_t cfa_pattern;
    uint32_t calibration_illuminant1;
    int32_t color_matrix1[18];
    uint32_t dynamic_range;
};

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
} mlv_hdr_t;

typedef struct {
    uint8_t     fileMagic[4];
    uint32_t    blockSize;
    uint8_t     versionString[8];
    uint64_t    fileGuid;
    uint16_t    fileNum;
    uint16_t    fileCount;
    uint32_t    fileFlags;
    uint16_t    videoClass;
    uint16_t    audioClass;
    uint32_t    videoFrameCount;
    uint32_t    audioFrameCount;
    uint32_t    sourceFpsNom;
    uint32_t    sourceFpsDenom;
}  mlv_file_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    frameNumber;
    uint16_t    cropPosX;
    uint16_t    cropPosY;
    uint16_t    panPosX;
    uint16_t    panPosY;
    uint32_t    frameSpace;
}  mlv_vidf_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    frameNumber;
    uint32_t    frameSpace;
}  mlv_audf_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint16_t    xRes;
    uint16_t    yRes;
    struct raw_info    raw_info;
}  mlv_rawi_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint16_t    format;
    uint16_t    channels;
    uint32_t    samplingRate;
    uint32_t    bytesPerSecond;
    uint16_t    blockAlign;
    uint16_t    bitsPerSample;
}  mlv_wavi_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    isoMode;
    uint32_t    isoValue;
    uint32_t    isoAnalog;
    uint32_t    digitalGain;
    uint64_t    shutterValue;
}  mlv_expo_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint16_t    focalLength;
    uint16_t    focalDist;
    uint16_t    aperture;
    uint8_t     stabilizerMode;
    uint8_t     autofocusMode;
    uint32_t    flags;
    uint32_t    lensID;
    uint8_t     lensName[32];
    uint8_t     lensSerial[32];
}  mlv_lens_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint16_t    tm_sec;
    uint16_t    tm_min;
    uint16_t    tm_hour;
    uint16_t    tm_mday;
    uint16_t    tm_mon;
    uint16_t    tm_year;
    uint16_t    tm_wday;
    uint16_t    tm_yday;
    uint16_t    tm_isdst;
    uint16_t    tm_gmtoff;
    uint8_t     tm_zone[8];
}  mlv_rtci_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint8_t     cameraName[32];
    uint32_t    cameraModel;
    uint8_t     cameraSerial[32];
}  mlv_idnt_hdr_t;

typedef struct {
    uint16_t    fileNumber;
    uint8_t     empty;
    uint8_t     frameType;
    uint64_t    frameOffset;
}  mlv_xref_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    frameType;
    uint32_t    entryCount;
}  mlv_xref_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
}  mlv_info_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    dualMode;
    uint32_t    isoValue;
}  mlv_diso_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    type;
}  mlv_mark_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    picStyleId;
    int32_t     contrast;
    int32_t     sharpness;
    int32_t     saturation;
    int32_t     colortone;
    uint8_t     picStyleName[16];
}  mlv_styl_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    roll;
    uint32_t    pitch;
}  mlv_elvl_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    wb_mode;
    uint32_t    kelvin;
    uint32_t    wbgain_r;
    uint32_t    wbgain_g;
    uint32_t    wbgain_b;
    uint32_t    wbs_gm;
    uint32_t    wbs_ba;
}  mlv_wbal_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    type;
    uint32_t    length;
}  mlv_debg_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    type;
    uint32_t    length;
}  mlv_colr_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint16_t sensor_res_x;
    uint16_t sensor_res_y;
    uint16_t sensor_crop;
    uint16_t reserved;
    uint8_t  binning_x;
    uint8_t  skipping_x;
    uint8_t  binning_y;
    uint8_t  skipping_y;
    int16_t  offset_x;
    int16_t  offset_y;
}  mlv_rawc_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    double      noiseProfile[6];
    uint32_t    lscWidth;
    uint32_t    lscHeight;
} mlv_c2md_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    float       colorMatrix2[9];
    float       forwardMatrix1[9];
    float       forwardMatrix2[9];
    float       calibration1[9];
    float       calibration2[9];
    uint16_t    illuminant1;
    uint16_t    illuminant2;
    uint16_t    blackLevel[4];
    uint32_t    activeArea[4];
    char        software[64];
} mlv_c2st_hdr_t;

typedef struct {
    uint8_t     blockType[4];
    uint32_t    blockSize;
    uint64_t    timestamp;
    uint32_t    active_w;
    uint32_t    active_h;
    uint32_t    offset_x;
    uint32_t    offset_y;
} mlv_crop_hdr_t;

#pragma pack(pop)

#endif