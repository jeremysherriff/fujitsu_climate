#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/automation.h"
#include "fujitsu_ir.h"

#include <vector>

namespace esphome {
namespace fujitsu_ac {

static const char *const TAG = "fujitsu_climate";
static constexpr float MIN_TEMP_HEAT = 16.0f;
static constexpr float MIN_TEMP_OTHER = 18.0f;
static constexpr float MAX_TEMP = 30.0f;

using ByteVector = std::vector<uint8_t>;

class FujitsuClimate : public climate::Climate, public Component {
 public:
  void set_room_sensor(sensor::Sensor *s) { this->room_sensor_ = s; }

  void add_on_frame_callback(std::function<void(ByteVector)> &&callback) {
    this->frame_callbacks_.add(std::move(callback));
  }

  void setup() override {
    auto restore = this->restore_state_();
    if (restore.has_value()) {
      restore->apply(this);
      ESP_LOGI(TAG, "Restored previous climate entity state");
    } else {
      this->mode = climate::CLIMATE_MODE_OFF;
      this->target_temperature = 22.0f;
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      this->swing_mode = climate::CLIMATE_SWING_OFF;
    }
    this->publish_state();
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Fujitsu AC Climate:");
    if (this->room_sensor_ != nullptr) {
      ESP_LOGCONFIG(TAG, "  Room Sensor: %s", this->room_sensor_->get_name().c_str());
    } else {
      ESP_LOGCONFIG(TAG, "  Room Sensor: none");
    }
    LOG_CLIMATE("  ", "Fujitsu AC", this);
  }

  climate::ClimateTraits traits() override {
    auto traits = climate::ClimateTraits();

    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);

    traits.set_supported_modes({
        climate::CLIMATE_MODE_OFF,
        climate::CLIMATE_MODE_COOL,
        climate::CLIMATE_MODE_HEAT,
        climate::CLIMATE_MODE_DRY,
        climate::CLIMATE_MODE_FAN_ONLY,
        climate::CLIMATE_MODE_HEAT_COOL,
    });

    traits.set_supported_fan_modes({
        climate::CLIMATE_FAN_AUTO,
        climate::CLIMATE_FAN_LOW,
        climate::CLIMATE_FAN_MEDIUM,
        climate::CLIMATE_FAN_HIGH,
        climate::CLIMATE_FAN_QUIET,
    });

    traits.set_supported_swing_modes({
        climate::CLIMATE_SWING_OFF,
        climate::CLIMATE_SWING_VERTICAL,
    });

    traits.set_visual_min_temperature(MIN_TEMP_HEAT);
    traits.set_visual_max_temperature(MAX_TEMP);
    traits.set_visual_temperature_step(1.0f);
    traits.set_visual_current_temperature_step(0.1f);

    return traits;
  }

  void loop() override {
    if (this->room_sensor_ == nullptr || !this->room_sensor_->has_state())
      return;

    float new_temp = this->round_to_tenth(this->room_sensor_->state);
    bool temp_changed = (this->current_temperature != new_temp);
    if (temp_changed)
      this->current_temperature = new_temp;

    bool correction_needed = false;
    if (this->needs_correction_) {
      // Add a small delay before publishing the corrected/clamped
      // temperature to avoid HA debouncing the update
      if (App.get_loop_component_start_time() - this->correction_requested_at_ > 200) {
        // MIN_TEMP_OTHER is always correct here since needs_correction_
        // is only set when mode is not HEAT
        this->target_temperature = MIN_TEMP_OTHER;
        this->needs_correction_ = false;
        correction_needed = true;
      }
    }

    if (temp_changed || correction_needed) {
      this->publish_state();
    }
  }

  void control(const climate::ClimateCall &call) override {

    // The physical remote enforces a minimum target temperature of 18°C in all modes
    // except HEAT, where 16°C is permitted. Clamp here to prevent sending signals
    // the unit will ignore.

    if (call.get_mode().has_value()) {
      this->mode = *call.get_mode();
      if (this->mode != climate::CLIMATE_MODE_HEAT) {
        float clamped = std::max(this->target_temperature, MIN_TEMP_OTHER);
        if (clamped != this->target_temperature) {
          ESP_LOGD(TAG, "Target temperature clamped to %.0f°C minimum for non-HEAT mode", MIN_TEMP_OTHER);
          this->target_temperature = clamped;
        }
      }
    }

    if (call.get_target_temperature().has_value()) {
      float requested = *call.get_target_temperature();
      float min_temp = (this->mode == climate::CLIMATE_MODE_HEAT) ? MIN_TEMP_HEAT : MIN_TEMP_OTHER;
      float clamped = std::max(requested, min_temp);
      if (clamped != requested) {
        ESP_LOGD(TAG, "Target temperature %.0f°C below minimum %.0f°C for current mode, clamping", requested, min_temp);
        this->needs_correction_ = true;
        this->correction_requested_at_ = App.get_loop_component_start_time();
      }
      this->target_temperature = requested;  // let HA see the unclamped value first
    }

    if (call.get_fan_mode().has_value())
      this->fan_mode = *call.get_fan_mode();

    if (call.get_swing_mode().has_value())
      this->swing_mode = *call.get_swing_mode();

    this->send_ir_frame_();
    this->publish_state();
  }

  bool send_powerful_() {
    if (this->mode == climate::CLIMATE_MODE_OFF) {
      ESP_LOGW(TAG, "Powerful command ignored — unit is OFF");
      return false;
    }
    uint8_t payload[5];
    if (fujitsu_ir::build_control_frame(payload, fujitsu_ir::CMD_POWERFUL)) {
      ByteVector frame(std::begin(payload), std::end(payload));
      ESP_LOGD(TAG, "Built 5-byte POWERFUL frame: %02X %02X %02X %02X %02X",
               payload[0], payload[1], payload[2], payload[3], payload[4]);
      this->frame_callbacks_.call(frame);
      return true;
    }
    return false;
  }

  void update_from_ir(
      const std::string &new_mode,
      int temp,
      const std::string &fan,
      const std::string &swing
  ) {
    using namespace climate;

    if      (new_mode == "cool")     this->mode = CLIMATE_MODE_COOL;
    else if (new_mode == "heat")     this->mode = CLIMATE_MODE_HEAT;
    else if (new_mode == "dry")      this->mode = CLIMATE_MODE_DRY;
    else if (new_mode == "fan_only") this->mode = CLIMATE_MODE_FAN_ONLY;
    else if (new_mode == "auto")     this->mode = CLIMATE_MODE_HEAT_COOL;
    else                             this->mode = CLIMATE_MODE_OFF;

    if (temp != -1) {
      float min_temp = (this->mode == climate::CLIMATE_MODE_HEAT) ? MIN_TEMP_HEAT : MIN_TEMP_OTHER;
      if (static_cast<float>(temp) < min_temp) {
        ESP_LOGW(TAG, "IR decoded temperature %.0f°C is below expected minimum %.0f°C for current mode",
                 static_cast<float>(temp), min_temp);
      }
      this->target_temperature = static_cast<float>(temp);
    }

    if      (fan == "auto")   this->fan_mode = CLIMATE_FAN_AUTO;
    else if (fan == "low")    this->fan_mode = CLIMATE_FAN_LOW;
    else if (fan == "medium") this->fan_mode = CLIMATE_FAN_MEDIUM;
    else if (fan == "high")   this->fan_mode = CLIMATE_FAN_HIGH;
    else if (fan == "quiet")  this->fan_mode = CLIMATE_FAN_QUIET;

    if      (swing == "off")      this->swing_mode = CLIMATE_SWING_OFF;
    else if (swing == "vertical") this->swing_mode = CLIMATE_SWING_VERTICAL;

    this->publish_state();
  }

 protected:
  CallbackManager<void(ByteVector)> frame_callbacks_;
  sensor::Sensor *room_sensor_{nullptr};
  bool needs_correction_{false};
  uint32_t correction_requested_at_{0};

  static inline float round_to_tenth(float x) {
    return roundf(x * 10.0f) / 10.0f;
  }

  void send_ir_frame_() {
    ByteVector frame;

    if (this->mode == climate::CLIMATE_MODE_OFF) {
      uint8_t payload[5];
      if (fujitsu_ir::build_control_frame(payload, fujitsu_ir::CMD_OFF)) {
        frame.assign(std::begin(payload), std::end(payload));
        ESP_LOGD(TAG, "Built 5-byte OFF frame: %02X %02X %02X %02X %02X",
                 payload[0], payload[1], payload[2], payload[3], payload[4]);
      }
    } else {
      uint8_t payload[14];
      auto fan = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
      // Use clamped target_temperature if correction is pending,
      // otherwise use current target_temperature
      float temp = this->needs_correction_ ?
          std::max(this->target_temperature, MIN_TEMP_OTHER) :
          this->target_temperature;
      if (fujitsu_ir::build_full_frame(payload, this->mode, temp, fan, this->swing_mode)) {
        frame.assign(std::begin(payload), std::end(payload));
        ESP_LOGD(TAG, "Built 14-byte frame: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 payload[0], payload[1], payload[2], payload[3], payload[4],
                 payload[5], payload[6], payload[7], payload[8], payload[9],
                 payload[10], payload[11], payload[12], payload[13]);
      }
    }

    if (!frame.empty()) {
      this->frame_callbacks_.call(frame);
    }
  }
};

class FujitsuClimateFrameTrigger : public Trigger<ByteVector> {
 public:
  explicit FujitsuClimateFrameTrigger(FujitsuClimate *parent) {
    parent->add_on_frame_callback([this](ByteVector frame) {
      this->trigger(frame);
    });
  }
};

}  // namespace fujitsu_ac
}  // namespace esphome
