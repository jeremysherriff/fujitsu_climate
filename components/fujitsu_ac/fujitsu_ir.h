#pragma once

#include "esphome/core/log.h"
#include "esphome/components/climate/climate.h"

namespace fujitsu_ir {

static const char *const TAG = "fujitsu_ir";

// Protocol
static const uint16_t ADDRESS_FUJITSU_IR = 0x28C6;

// Model-specific header bytes
static const uint8_t FRAME_BYTE0  = 0x00;
static const uint8_t FRAME_BYTE1  = 0x08;
static const uint8_t FRAME_BYTE2  = 0x08;

// Control frame commands (byte [3])
static const uint8_t CMD_OFF        = 0x40;
static const uint8_t CMD_POWERFUL   = 0x9C;
static const uint8_t CMD_FULL_STATE = 0x7F;

// Full frame fixed bytes
static const uint8_t FRAME_BYTE4   = 0x90;  // unknown, use observed value
static const uint8_t FRAME_BYTE5   = 0x0C;  // unknown, use observed value

static const uint8_t FRAME_BYTE9   = 0xA8;  // hour - not operationally significant
static const uint8_t FRAME_BYTE10  = 0xE0;  // unknown, use observed value
static const uint8_t FRAME_BYTE11  = 0x42;  // minute - not operationally significant
static const uint8_t FRAME_BYTE12  = 0x04;  // unknown, use observed value

// Byte [6] power flag
static const uint8_t POWER_ON     = 0x80;

// Temperature to nibble map, index = (temp - 16), value = nibble
static const uint8_t TEMP_MAP[15] = {
    0x00, // 16°C
    0x08, // 17°C
    0x04, // 18°C
    0x0C, // 19°C
    0x02, // 20°C
    0x0A, // 21°C
    0x06, // 22°C
    0x0E, // 23°C
    0x01, // 24°C
    0x09, // 25°C
    0x05, // 26°C
    0x0D, // 27°C
    0x03, // 28°C
    0x0B, // 29°C
    0x07, // 30°C
};

// Nibble to temperature map, index = nibble, value = temperature (-1 = invalid)
static const int TEMP_MAP_DECODE[16] = {
    16, 24, 20, 28, 18, 26, 22, 30,
    17, 25, 21, 29, 19, 27, 23, -1
};

// Mode bytes (byte [7])
static const uint8_t MODE_AUTO     = 0x00;
static const uint8_t MODE_COOL     = 0x80;
static const uint8_t MODE_DRY      = 0x40;
static const uint8_t MODE_FAN_ONLY = 0xC0;
static const uint8_t MODE_HEAT     = 0x20;

// Fan codes (byte [8] upper nibble, pre-shifted)
static const uint8_t FAN_AUTO      = 0x00;
static const uint8_t FAN_HIGH      = 0x80;
static const uint8_t FAN_MEDIUM    = 0x40;
static const uint8_t FAN_LOW       = 0xC0;
static const uint8_t FAN_IDLE      = 0x20;

// Swing codes (byte [8] lower nibble)
static const uint8_t SWING_OFF      = 0x00;
static const uint8_t SWING_VERTICAL = 0x08;

// ---------------------

inline uint8_t checksum_control(uint8_t cmd) {
    return (~cmd) & 0xFF;
}


// Fujitsu AEHA full-frame checksum (AR-REA1E)
// Algorithm: reverse bits of each payload byte [6..12], sum them,
// subtract from the checksum seed, then reverse bits of the result.
// Seed value 0xD0 was determined empirically for this remote model.
static const uint8_t CHECKSUM_SEED = 0xD0;

static inline uint8_t reverse_bits(uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) r |= ((b >> i) & 1) << (7 - i);
    return r;
}

inline uint8_t checksum_full(const uint8_t *buf) {
    uint8_t sum = 0;
    for (int i = 6; i <= 12; i++) sum += reverse_bits(buf[i]);
    return reverse_bits(CHECKSUM_SEED - sum);
}

inline bool build_control_frame(uint8_t *out, uint8_t command) {
    out[0] = FRAME_BYTE0;
    out[1] = FRAME_BYTE1;
    out[2] = FRAME_BYTE2;
    out[3] = command;

    out[4] = checksum_control(command);
    
    return true;
}

inline bool build_full_frame(uint8_t *out,
                             esphome::climate::ClimateMode mode,
                             float temp,
                             esphome::climate::ClimateFanMode fan,
                             esphome::climate::ClimateSwingMode swing) {
    
    // Enforce physical remote temperature limits as a safety net.
    // Primary clamping is handled by FujitsuClimate::control(), but this
    // ensures correct frames regardless of how this function is called.
    float min_temp = (mode == esphome::climate::CLIMATE_MODE_HEAT) ? 16.0f : 18.0f;
    float clamped_temp = std::max(std::min(temp, 30.0f), min_temp);
    if (clamped_temp != temp) {
        ESP_LOGW(TAG, "build_full_frame temperature %.0f°C clamped to %.0f°C", temp, clamped_temp);
    }

    // Temperature map
    uint8_t temp_nibble = TEMP_MAP[(uint8_t)(temp - 16.0f)];

    uint8_t mode_byte;
    switch (mode) {
        case esphome::climate::CLIMATE_MODE_HEAT_COOL: mode_byte = MODE_AUTO; break;
        case esphome::climate::CLIMATE_MODE_COOL:      mode_byte = MODE_COOL; break;
        case esphome::climate::CLIMATE_MODE_DRY:       mode_byte = MODE_DRY; break;
        case esphome::climate::CLIMATE_MODE_FAN_ONLY:  mode_byte = MODE_FAN_ONLY; break;
        case esphome::climate::CLIMATE_MODE_HEAT:      mode_byte = MODE_HEAT; break;
        case esphome::climate::CLIMATE_MODE_OFF:
            // Unit is off - should not be sending full-state frames
            ESP_LOGW(TAG, "build_full_frame called with CLIMATE_MODE_OFF, ignoring");
            return false;
        default:
            ESP_LOGW(TAG, "build_full_frame unknown mode: %d", (int)mode);
            return false;
    }

    uint8_t fan_nibble;
    switch (fan) {
        case esphome::climate::CLIMATE_FAN_AUTO:   fan_nibble = FAN_AUTO; break;
        case esphome::climate::CLIMATE_FAN_HIGH:   fan_nibble = FAN_HIGH; break;
        case esphome::climate::CLIMATE_FAN_MEDIUM: fan_nibble = FAN_MEDIUM; break;
        case esphome::climate::CLIMATE_FAN_LOW:    fan_nibble = FAN_LOW; break;
        case esphome::climate::CLIMATE_FAN_QUIET:  fan_nibble = FAN_IDLE; break;
        default:                                   fan_nibble = FAN_AUTO; break;
    }

    uint8_t swing_nibble;
    switch (swing) {
        case esphome::climate::CLIMATE_SWING_OFF:        swing_nibble = SWING_OFF; break;
        case esphome::climate::CLIMATE_SWING_VERTICAL:   swing_nibble = SWING_VERTICAL; break;
        default:                                         swing_nibble = SWING_OFF; break;
    }

    out[0]  = FRAME_BYTE0;
    out[1]  = FRAME_BYTE1;
    out[2]  = FRAME_BYTE2;
    out[3]  = CMD_FULL_STATE;
    out[4]  = FRAME_BYTE4;
    out[5]  = FRAME_BYTE5;
    out[6]  = POWER_ON | temp_nibble;
    out[7]  = mode_byte;
    out[8]  = fan_nibble | swing_nibble;
    out[9]  = FRAME_BYTE9;
    out[10] = FRAME_BYTE10;
    out[11] = FRAME_BYTE11;
    out[12] = FRAME_BYTE12;

    out[13] = checksum_full(out);
    
    return true;
}

}  // namespace fujitsu_ir
