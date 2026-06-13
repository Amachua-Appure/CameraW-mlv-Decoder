#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <limits>
#include <sstream> 

#define TINY_DNG_WRITER_IMPLEMENTATION
#include "tiny_dng_writer.h"
#include "mlv_structs.h"
#include "AudioFile.h"

#pragma pack(push, 1)
struct MlvHdr {
    uint8_t type[4];
    uint32_t size;
};

struct CustomWbal {
    uint8_t type[4];
    uint32_t size;
    uint64_t timestamp;
    uint32_t wb_r;
    uint32_t wb_g;
    uint32_t wb_b;
    uint32_t wb_g2;
};

struct GyroSample {
    uint64_t timestamp; 
    float pitch_rate;  
    float yaw_rate;     
    float roll_rate;    
    uint32_t pad;      
};
#pragma pack(pop)

static std::vector<double> current_noise_profile;
static std::vector<float> current_lsc_map;
static int current_lsc_w = 0, current_lsc_h = 0;

static float current_focus_distance = 0.0f;
static int current_lens_state = 0;
static uint64_t current_rolling_shutter = 0;
static uint64_t current_frame_duration = 0;

static float current_dyn_bl[4] = {0.0f};
static float current_neutral[3] = {0.0f};
static uint32_t current_dyn_wl = 0;

static std::vector<GyroSample> global_gyro_data;

static std::vector<double> initial_noise_profile;
static std::vector<float> initial_lsc_map;
static int initial_lsc_w = 0, initial_lsc_h = 0;

static bool has_c2st = false;
static mlv_c2st_hdr_t global_c2st;

static bool has_crop = false;
static mlv_crop_hdr_t global_crop;

static void cropLSCMap() {
    if (!has_crop || !has_c2st) {
        return;
    }
    if (current_lsc_map.empty() || current_lsc_w == 0 || current_lsc_h == 0) {
        return;
    }
    if (global_crop.active_w == 0 || global_crop.active_h == 0) {
        return;
    }

    if (global_crop.offset_x == 0 && global_crop.offset_y == 0 &&
        global_c2st.activeArea[1] == 0 && global_c2st.activeArea[0] == 0) {
    }

    std::cout << "[LSC] Interpolating Open Gate LSC map to fit cropped RAW window..." << std::endl;

    const int channels = 4;
    std::vector<float> new_lsc_map(current_lsc_w * current_lsc_h * channels);

    for (int gy = 0; gy < current_lsc_h; ++gy) {
        for (int gx = 0; gx < current_lsc_w; ++gx) {
            float u = (current_lsc_w > 1) ? (float)gx / (current_lsc_w - 1) : 0.5f;
            float v = (current_lsc_h > 1) ? (float)gy / (current_lsc_h - 1) : 0.5f;

            float px = global_crop.offset_x + u * global_crop.active_w;
            float py = global_crop.offset_y + v * global_crop.active_h;

            float full_u = px / global_crop.active_w;
            float full_v = py / global_crop.active_h;

            full_u = std::max(0.0f, std::min(full_u, 1.0f));
            full_v = std::max(0.0f, std::min(full_v, 1.0f));

            float src_gx = full_u * (current_lsc_w - 1);
            float src_gy = full_v * (current_lsc_h - 1);

            int x0 = std::min((int)src_gx, current_lsc_w - 1);
            int x1 = std::min(x0 + 1, current_lsc_w - 1);
            int y0 = std::min((int)src_gy, current_lsc_h - 1);
            int y1 = std::min(y0 + 1, current_lsc_h - 1);

            float dx = src_gx - x0;
            float dy = src_gy - y0;

            for (int c = 0; c < channels; ++c) {
                float val00 = current_lsc_map[(y0 * current_lsc_w + x0) * channels + c];
                float val10 = current_lsc_map[(y0 * current_lsc_w + x1) * channels + c];
                float val01 = current_lsc_map[(y1 * current_lsc_w + x0) * channels + c];
                float val11 = current_lsc_map[(y1 * current_lsc_w + x1) * channels + c];

                float val0 = val00 * (1.0f - dx) + val10 * dx;
                float val1 = val01 * (1.0f - dx) + val11 * dx;
                float finalVal = val0 * (1.0f - dy) + val1 * dy;

                new_lsc_map[(gy * current_lsc_w + gx) * channels + c] = finalVal;
            }
        }
    }

    current_lsc_map = std::move(new_lsc_map);
    std::cout << "[LSC] Done. LSC map aligned to cropped RAW window." << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.mlv>\n";
        return 1;
    }

    std::string input_filename = argv[1];
    std::ifstream file(input_filename, std::ios::binary);
    if (!file) {
        std::cerr << "Could not open file.\n";
        return 1;
    }

    int width = 0, height = 0, frame_count = 0;
    int black_lvl = 64, white_lvl = 1023;
    uint32_t cfa_pattern = 0x02010100;
    float color_matrix[9] = {1,0,0, 0,1,0, 0,0,1};
    float as_shot_neutral[3] = {1.0f, 1.0f, 1.0f};
    int iso = 0;
    uint32_t shutter_us = 0;  
    float focal_length = 0.0f;
    float aperture = 0.0f;
    char date_time[64] = {0};

    std::vector<int16_t> raw_audio_data;
    int audio_sample_rate = 48000;
    int audio_channels = 2;

    std::cout << "Pass 1: Scanning for Audio and Gyro..." << std::endl;

    while (file) {
        MlvHdr hdr;
        if (!file.read((char*)&hdr, sizeof(hdr))) break;
        if (hdr.size < 8) break;

        std::string typeStr((char*)hdr.type, 4);

        if (typeStr == "WAVI") {
            mlv_wavi_hdr_t wavi;
            file.seekg(-8, std::ios::cur);
            file.read((char*)&wavi, sizeof(wavi));
            audio_sample_rate = wavi.samplingRate;
            audio_channels = wavi.channels;
            file.seekg(hdr.size - sizeof(wavi), std::ios::cur);

        } else if (typeStr == "AUDF") {
            mlv_audf_hdr_t audf;
            file.seekg(-8, std::ios::cur);
            file.read((char*)&audf, sizeof(audf));

            size_t pcm_size = hdr.size - sizeof(audf);
            if (pcm_size > 0) {
                std::vector<int16_t> temp_pcm(pcm_size / sizeof(int16_t));
                file.read((char*)temp_pcm.data(), pcm_size);
                raw_audio_data.insert(raw_audio_data.end(), temp_pcm.begin(), temp_pcm.end());
            }
            int align_pad = (4 - (pcm_size % 4)) % 4;
            if (align_pad > 0) {
                file.seekg(align_pad, std::ios::cur);
            }

        } else if (typeStr == "C2MD" && initial_lsc_map.empty()) {
            mlv_c2md_hdr_t c2md;
            file.seekg(-8, std::ios::cur);
            file.read((char*)&c2md, sizeof(c2md));

            initial_noise_profile.assign(c2md.noiseProfile, c2md.noiseProfile + 8);

            size_t lsc_size = c2md.lscWidth * c2md.lscHeight * 4;
            if (lsc_size > 0) {
                initial_lsc_map.resize(lsc_size);
                file.read((char*)initial_lsc_map.data(), lsc_size * sizeof(float));
                initial_lsc_w = c2md.lscWidth;
                initial_lsc_h = c2md.lscHeight;
            }

            int bytes_read = sizeof(c2md) + (lsc_size * sizeof(float));
            if (hdr.size > bytes_read) {
                file.seekg(hdr.size - bytes_read, std::ios::cur);
            }

        } else if (typeStr == "GYRO") {
            uint64_t gyro_ts;
            file.read((char*)&gyro_ts, 8); 
            size_t payload_size = hdr.size - 16; 
            size_t num_samples = payload_size / sizeof(GyroSample);
            
            for (size_t i = 0; i < num_samples; i++) {
                GyroSample sample;
                file.read((char*)&sample, sizeof(GyroSample));
                global_gyro_data.push_back(sample);
            }

            size_t bytes_read = 16 + (num_samples * sizeof(GyroSample));
            if (hdr.size > bytes_read) {
                file.seekg(hdr.size - bytes_read, std::ios::cur);
            }

        } else {
            file.seekg(hdr.size - 8, std::ios::cur);
        }
    }

    if (!raw_audio_data.empty()) {
        std::cout << "Extracting " << (raw_audio_data.size() / audio_channels) << " audio samples..." << std::endl;
        AudioFile<double> audioFile;
        audioFile.setSampleRate(audio_sample_rate);
        audioFile.setBitDepth(16);
        audioFile.setNumChannels(audio_channels);

        int num_samples = raw_audio_data.size() / audio_channels;
        audioFile.setNumSamplesPerChannel(num_samples);

        for (int i = 0; i < num_samples; i++) {
            for (int c = 0; c < audio_channels; c++) {
                int16_t sample = raw_audio_data[i * audio_channels + c];
                audioFile.samples[c][i] = sample / 32768.0;
            }
        }
        audioFile.save("audio.wav");
        std::cout << "Saved audio.wav\n" << std::endl;

        raw_audio_data.clear();
        raw_audio_data.shrink_to_fit();
    } else {
        std::cout << "No audio stream found in MLV.\n" << std::endl;
    }

    if (!global_gyro_data.empty()) {
        std::sort(global_gyro_data.begin(), global_gyro_data.end(),
                  [](const GyroSample& a, const GyroSample& b) {
                      return a.timestamp < b.timestamp;
                  });

        std::string gcsv_filename = input_filename + ".gcsv";
        std::ofstream gcsv(gcsv_filename);
        if (gcsv) {
            gcsv << "GYROFLOW IMU LOG\n"
                 << "version,1.2\n"
                 << "id,CameraW\n"
                 << "orientation,YxZ\n"
                 << "tscale,1e-09\n\n";
            gcsv << "timestamp,gx,gy,gz\n";
            
            for (const auto& sample : global_gyro_data) {
                double timestamp_us = sample.timestamp / 1000.0;
                gcsv << std::fixed << timestamp_us << "," 
                     << sample.pitch_rate << "," << sample.yaw_rate << "," << sample.roll_rate << "\n";
            }
            gcsv.close();
            std::cout << "Extracted Gyroflow Sidecar: " << gcsv_filename << "\n\n";
        }
        global_gyro_data.clear();
        global_gyro_data.shrink_to_fit();
    }

    std::cout << "Pass 2: Extracting DNG frames..." << std::endl;

    current_noise_profile = initial_noise_profile;
    current_lsc_map = initial_lsc_map;
    current_lsc_w = initial_lsc_w;
    current_lsc_h = initial_lsc_h;

    file.clear();
    file.seekg(0, std::ios::beg);

    while (file) {
        MlvHdr hdr;
        if (!file.read((char*)&hdr, sizeof(hdr))) break;
        if (hdr.size < 8) break;

        std::string typeStr((char*)hdr.type, 4);

        if (typeStr == "RAWI") {
            mlv_rawi_hdr_t rawi;
            file.seekg(-8, std::ios::cur);
            file.read((char*)&rawi, sizeof(rawi));

            width = rawi.xRes;
            height = rawi.yRes;
            black_lvl = rawi.raw_info.black_level;
            white_lvl = rawi.raw_info.white_level;
            cfa_pattern = rawi.raw_info.cfa_pattern;

            for (int i = 0; i < 9; i++) {
                color_matrix[i] = (float)rawi.raw_info.color_matrix1[i*2] /
                                  (float)rawi.raw_info.color_matrix1[i*2+1];
            }

            file.seekg(hdr.size - sizeof(rawi), std::ios::cur);
            std::cout << "Found Video Stream: " << width << "x" << height << "\n";

        } else if (typeStr == "EXPO") {
            mlv_expo_hdr_t expo;
            file.seekg(-8, std::ios::cur);
            file.read((char*)&expo, sizeof(expo));

            iso = expo.isoValue;
            shutter_us = expo.shutterValue; 

            file.seekg(hdr.size - sizeof(expo), std::ios::cur);

        } else if (typeStr == "WBAL") {
            CustomWbal wbal;
            file.seekg(-8, std::ios::cur);
            file.read((char*)&wbal, sizeof(wbal));

            as_shot_neutral[0] = 1024.0f / (float)wbal.wb_r;
            as_shot_neutral[1] = 1024.0f / (float)wbal.wb_g;
            as_shot_neutral[2] = 1024.0f / (float)wbal.wb_b;
            as_shot_neutral[0] /= as_shot_neutral[1];
            as_shot_neutral[2] /= as_shot_neutral[1];
            as_shot_neutral[1] = 1.0f;

            file.seekg(hdr.size - sizeof(wbal), std::ios::cur);

        } else if (typeStr == "LENS") {
            mlv_lens_hdr_t lens;
            file.seekg(-8, std::ios::cur);
            file.read((char*)&lens, sizeof(lens));

            focal_length = lens.focalLength / 100.0f;
            aperture = lens.aperture / 100.0f;

            file.seekg(hdr.size - sizeof(lens), std::ios::cur);

        } else if (typeStr == "RTCI") {
            mlv_rtci_hdr_t rtci;
            file.seekg(-8, std::ios::cur);
            file.read((char*)&rtci, sizeof(rtci));

            snprintf(date_time, sizeof(date_time), "%04d:%02d:%02d %02d:%02d:%02d",
                     rtci.tm_year + 1900, rtci.tm_mon + 1, rtci.tm_mday,
                     rtci.tm_hour, rtci.tm_min, rtci.tm_sec);

            file.seekg(hdr.size - sizeof(rtci), std::ios::cur);

        } else if (typeStr == "C2ST") {
            file.seekg(-8, std::ios::cur);
            file.read((char*)&global_c2st, sizeof(global_c2st));
            has_c2st = true;
            file.seekg(hdr.size - sizeof(global_c2st), std::ios::cur);
            std::cout << "Loaded static C2ST metadata (DNG parity)\n";

        } else if (typeStr == "C2MD") {
            mlv_c2md_hdr_t c2md;
            file.seekg(-8, std::ios::cur);
            file.read((char*)&c2md, sizeof(c2md));

            current_noise_profile.assign(c2md.noiseProfile, c2md.noiseProfile + 8);

            size_t lsc_size = c2md.lscWidth * c2md.lscHeight * 4;
            if (lsc_size > 0) {
                current_lsc_map.resize(lsc_size);
                file.read((char*)current_lsc_map.data(), lsc_size * sizeof(float));
            }
            current_lsc_w = c2md.lscWidth;
            current_lsc_h = c2md.lscHeight;

            current_focus_distance = c2md.focusDistance;
            current_lens_state = c2md.lensState;
            current_rolling_shutter = c2md.rollingShutterSkew;
            current_frame_duration = c2md.frameDuration;

            memcpy(current_dyn_bl, c2md.dynamicBlackLevel, sizeof(current_dyn_bl));
            memcpy(current_neutral, c2md.neutralColorPoint, sizeof(current_neutral));
            current_dyn_wl = c2md.dynamicWhiteLevel;

            if (has_crop) {
                cropLSCMap();
            }

            int bytes_read = sizeof(c2md) + (lsc_size * sizeof(float));
            if (hdr.size > bytes_read) {
                file.seekg(hdr.size - bytes_read, std::ios::cur);
            }

        } else if (typeStr == "CROP") {
            file.seekg(-8, std::ios::cur);
            file.read((char*)&global_crop, sizeof(global_crop));
            has_crop = true;
            file.seekg(hdr.size - sizeof(global_crop), std::ios::cur);
            std::cout << "Loaded CROP metadata: active=" << global_crop.active_w << "x" << global_crop.active_h
                      << " offset=(" << global_crop.offset_x << "," << global_crop.offset_y << ")\n";

        } else if (typeStr == "GYRO") {
            file.seekg(hdr.size - 8, std::ios::cur);

        } else if (typeStr == "VIDF") {
            file.seekg(24, std::ios::cur);
            size_t payload_size = hdr.size - 32;
            std::vector<uint8_t> payload(payload_size);
            file.read((char*)payload.data(), payload_size);

            if (width > 0 && height > 0) {
                std::vector<uint16_t> out_frame(width * height);

                int in_idx = 0;

                for (int y = 0; y < height; ++y) {
                    int x = 0;

                    for (; x <= width - 64; x += 64) {
                        if (in_idx + 5 > payload_size) break;

                        uint8_t h0 = payload[in_idx++];
                        uint8_t h1 = payload[in_idx++];
                        uint8_t h2 = payload[in_idx++];
                        uint8_t h3 = payload[in_idx++];
                        uint8_t h4 = payload[in_idx++];

                        int bits = h0;
                        uint16_t min_even = (h1 << 8) | h2;
                        uint16_t min_odd  = (h3 << 8) | h4;

                        int pixel_idx = y * width + x;

                        if (bits == 0) {
                            for (int j = 0; j < 64; j++) {
                                out_frame[pixel_idx + j] = (j % 2 == 0) ? min_even : min_odd;
                            }
                            continue;
                        }

                        if (bits == 8) {
                            for (int j = 0; j < 64; j += 2) {
                                out_frame[pixel_idx + j]     = min_even + payload[in_idx++];
                                out_frame[pixel_idx + j + 1] = min_odd  + payload[in_idx++];
                            }
                            continue;
                        }

                        if (bits == 10) {
                            for (int j = 0; j < 64; j += 4) {
                                uint8_t b0 = payload[in_idx++];
                                uint8_t b1 = payload[in_idx++];
                                uint8_t b2 = payload[in_idx++];
                                uint8_t b3 = payload[in_idx++];
                                uint8_t b4 = payload[in_idx++];

                                uint16_t p0 = (b0 << 2) | (b1 >> 6);
                                uint16_t p1 = ((b1 & 0x3F) << 4) | (b2 >> 4);
                                uint16_t p2 = ((b2 & 0x0F) << 6) | (b3 >> 2);
                                uint16_t p3 = ((b3 & 0x03) << 8) | b4;

                                out_frame[pixel_idx + j]     = min_even + p0;
                                out_frame[pixel_idx + j + 1] = min_odd  + p1;
                                out_frame[pixel_idx + j + 2] = min_even + p2;
                                out_frame[pixel_idx + j + 3] = min_odd  + p3;
                            }
                            continue;
                        }

                        uint32_t bit_buf = 0;
                        int bit_cnt = 0;
                        for (int j = 0; j < 64; j++) {
                            while (bit_cnt < bits) {
                                if (in_idx >= payload_size) break;
                                bit_buf = (bit_buf << 8) | payload[in_idx++];
                                bit_cnt += 8;
                            }
                            uint16_t res = (bit_buf >> (bit_cnt - bits)) & ((1U << bits) - 1);
                            bit_cnt -= bits;

                            out_frame[pixel_idx + j] = ((j % 2 == 0) ? min_even : min_odd) + res;
                        }
                    }

                    for (; x < width; x++) {
                        if (in_idx + 2 > payload_size) break;
                        uint8_t high = payload[in_idx++];
                        uint8_t low = payload[in_idx++];
                        out_frame[y * width + x] = (high << 8) | low;
                    }
                }

                if (!current_lsc_map.empty() && current_lsc_w > 0 && current_lsc_h > 0) {
                    int channel_map[4][4] = {
                        {0, 1, 2, 3},
                        {1, 0, 3, 2},
                        {1, 3, 0, 2},
                        {3, 1, 2, 0}
                    };
                    int pattern_idx = (cfa_pattern <= 3) ? cfa_pattern : 0;

                    for (int y = 0; y < height; ++y) {
                        float v = (height > 1) ? (float)y / (height - 1) : 0.5f;
                        float src_gy = v * (current_lsc_h - 1);
                        int y0 = std::min((int)src_gy, current_lsc_h - 1);
                        int y1 = std::min(y0 + 1, current_lsc_h - 1);
                        float dy = src_gy - y0;
                        float inv_dy = 1.0f - dy;

                        int bayer_y = (y % 2) * 2;

                        for (int x = 0; x < width; ++x) {
                            float u = (width > 1) ? (float)x / (width - 1) : 0.5f;
                            float src_gx = u * (current_lsc_w - 1);
                            int x0 = std::min((int)src_gx, current_lsc_w - 1);
                            int x1 = std::min(x0 + 1, current_lsc_w - 1);
                            float dx = src_gx - x0;
                            float inv_dx = 1.0f - dx;

                            int bayer_index = bayer_y + (x % 2);
                            int c = channel_map[pattern_idx][bayer_index];

                            int pixel_bl = has_c2st ? global_c2st.blackLevel[bayer_index] : black_lvl;

                            int idx00 = (y0 * current_lsc_w + x0) * 4 + c;
                            int idx10 = (y0 * current_lsc_w + x1) * 4 + c;
                            int idx01 = (y1 * current_lsc_w + x0) * 4 + c;
                            int idx11 = (y1 * current_lsc_w + x1) * 4 + c;

                            float val0 = current_lsc_map[idx00] * inv_dx + current_lsc_map[idx10] * dx;
                            float val1 = current_lsc_map[idx01] * inv_dx + current_lsc_map[idx11] * dx;
                            float gain = val0 * inv_dy + val1 * dy;

                            int pixel_idx = y * width + x;
                            float pixel_val = out_frame[pixel_idx];
                            float corrected = ((pixel_val - pixel_bl) * gain) + pixel_bl;
                            out_frame[pixel_idx] = (uint16_t)std::max(0.0f, std::min(65535.0f, corrected));
                        }
                    }
                }

                tinydngwriter::DNGImage dng;
                dng.SetBigEndian(false);
                dng.SetImageWidth(width);
                dng.SetImageLength(height);

                dng.SetDNGVersion(1, 4, 0, 0);
                dng.SetDNGBackwardVersion(1, 1, 0, 0);
                dng.SetMake("CameraW");

                dng.SetColorMatrix1(3, color_matrix);
                
                if (current_neutral[0] > 0.0f) 
                    dng.SetAsShotNeutral(3, current_neutral);
                else 
                    dng.SetAsShotNeutral(3, as_shot_neutral);
                
                dng.SetIso(iso);
                
                if (shutter_us > 0) dng.SetRationalTag(33434, shutter_us, 1000000);
                
                if (focal_length > 0) dng.SetFocalLength(focal_length);
                if (aperture > 0) dng.SetAperture(aperture);

                dng.SetWhiteLevel(current_dyn_wl > 0 ? current_dyn_wl : white_lvl);

                if (current_dyn_bl[0] > 0.0f) {
                    unsigned short bl[4] = { (unsigned short)current_dyn_bl[0],
                                             (unsigned short)current_dyn_bl[1],
                                             (unsigned short)current_dyn_bl[2],
                                             (unsigned short)current_dyn_bl[3] };
                    dng.SetBlackLevelRepeatDim(2, 2);
                    dng.SetBlackLevel(4, bl);
                } else if (has_c2st) {
                    unsigned short bl[4] = { global_c2st.blackLevel[0], global_c2st.blackLevel[1],
                                             global_c2st.blackLevel[2], global_c2st.blackLevel[3] };
                    dng.SetBlackLevelRepeatDim(2, 2);
                    dng.SetBlackLevel(4, bl);
                } else {
                    unsigned short bl[1] = { (unsigned short)black_lvl };
                    dng.SetBlackLevelRepeatDim(1, 1);
                    dng.SetBlackLevel(1, bl);
                }

                dng.SetSamplesPerPixel(1);
                unsigned short bps[1] = {16};
                dng.SetBitsPerSample(1, bps);
                dng.SetRowsPerStrip(height);
                dng.SetCFALayout(1);
                dng.SetPlanarConfig(tinydngwriter::PLANARCONFIG_CONTIG);
                dng.SetCompression(tinydngwriter::COMPRESSION_NONE);
                dng.SetPhotometric(tinydngwriter::PHOTOMETRIC_CFA);

                unsigned short cfa_repeat[2] = {2, 2};
                dng.SetCFARepeatPatternDim(cfa_repeat[0], cfa_repeat[1]);
                uint8_t cfa_array[4];

                if (cfa_pattern == 0) {
                    cfa_array[0] = 0; cfa_array[1] = 1; cfa_array[2] = 1; cfa_array[3] = 2;
                } else if (cfa_pattern == 1) {
                    cfa_array[0] = 1; cfa_array[1] = 0; cfa_array[2] = 2; cfa_array[3] = 1;
                } else if (cfa_pattern == 2) {
                    cfa_array[0] = 1; cfa_array[1] = 2; cfa_array[2] = 0; cfa_array[3] = 1;
                } else if (cfa_pattern == 3) {
                    cfa_array[0] = 2; cfa_array[1] = 1; cfa_array[2] = 1; cfa_array[3] = 0;
                } else {
                    cfa_array[0] =  cfa_pattern        & 0xFF;
                    cfa_array[1] = (cfa_pattern >> 8)  & 0xFF;
                    cfa_array[2] = (cfa_pattern >> 16) & 0xFF;
                    cfa_array[3] = (cfa_pattern >> 24) & 0xFF;
                }
                dng.SetCFAPattern(4, cfa_array);

                if (has_c2st) {
                    dng.SetStringTag(305, global_c2st.software);
                    
                    unsigned int activeArea[4] = {
                        (unsigned int)global_c2st.activeArea[0], 
                        (unsigned int)global_c2st.activeArea[1], 
                        (unsigned int)global_c2st.activeArea[2], 
                        (unsigned int)global_c2st.activeArea[3]  
                    };
                    dng.SetLongArrayTag(50829, 4, activeArea);
                    dng.SetShortTag(50778, global_c2st.illuminant1);
                    dng.SetShortTag(50779, global_c2st.illuminant2);
                    dng.SetMatrixTag(50722, global_c2st.colorMatrix2);
                    dng.SetMatrixTag(50964, global_c2st.forwardMatrix1);
                    dng.SetMatrixTag(50965, global_c2st.forwardMatrix2);
                    dng.SetMatrixTag(50768, global_c2st.calibration1);
                    dng.SetMatrixTag(50769, global_c2st.calibration2);
                }

                if (!current_noise_profile.empty()) {
                    dng.SetNoiseProfile(current_noise_profile.size(), current_noise_profile.data());
                }

                std::stringstream xmp;
                xmp << "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                    << "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
                    << "  <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
                    << "    <rdf:Description rdf:about=\"\" xmlns:cameraw=\"cameraw/2.4/\">\n"
                    << "      <cameraw:FocusDistance>" << current_focus_distance << "</cameraw:FocusDistance>\n"
                    << "      <cameraw:LensState>" << current_lens_state << "</cameraw:LensState>\n"
                    << "      <cameraw:RollingShutterSkew>" << current_rolling_shutter << "</cameraw:RollingShutterSkew>\n"
                    << "      <cameraw:FrameDuration>" << current_frame_duration << "</cameraw:FrameDuration>\n"
                    << "    </rdf:Description>\n"
                    << "  </rdf:RDF>\n"
                    << "</x:xmpmeta>\n"
                    << "<?xpacket end=\"w\"?>";
                
                std::string xmp_str = xmp.str();
                dng.SetByteArrayTag(700, reinterpret_cast<const unsigned char*>(xmp_str.c_str()), xmp_str.length());

                dng.SetImageData(reinterpret_cast<unsigned char*>(out_frame.data()), width * height * 2);

                char filename[256];
                snprintf(filename, sizeof(filename), "frame_%04d.dng", frame_count++);

                tinydngwriter::DNGWriter writer(false);
                writer.AddImage(&dng);

                std::string err;
                if (writer.WriteToFile(filename, &err)) {
                    std::cout << "Extracted: " << filename << "\n";
                } else {
                    std::cerr << "Error writing " << filename << ": " << err << "\n";
                }
            }
        } else {
            file.seekg(hdr.size - 8, std::ios::cur);
        }
    }

    std::cout << "\nFinished successfully!\n";
    return 0;
}
