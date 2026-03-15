# fujitsu_climate

An ESPHome external component for Fujitsu Designer series air conditioners using the **AR-REA1E** remote control.

This component provides a native ESPHome climate entity, IR frame encoding and decoding, and full Home Assistant integration. It was developed by reverse-engineering captured IR frames from the AR-REA1E remote and is specifically designed for **14-byte frame** models. Existing community implementations targeting 15-byte frame Fujitsu remotes will not work with these units.

## Supported Hardware

| AC Unit | Remote |
|---|---|
| Fujitsu ASTG09KUCA | AR-REA1E |
| Fujitsu ASTG12KUCA | AR-REA1E |
| Fujitsu ASTG18KUCA | AR-REA1E |

Other models in the Fujitsu Designer ASTG\*KUCA range may also be compatible. If you have confirmed compatibility with another model, please open an issue or pull request.

> **Note:** This component uses 14-byte full-state frames. It is **not** compatible with Fujitsu models that use 15-byte frames, which are a different protocol variant used by other Fujitsu remotes.

## Requirements

- ESP32-based device
- IR transmitter LED connected to a GPIO pin
- IR receiver connected to a GPIO pin (optional, for physical remote state tracking)
- ESPHome 2024.1.0 or later

## Installation

Add the external component to your ESPHome configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/jeremysherriff/fujitsu_climate
    components: [fujitsu_ac]
```

## Configuration

### Minimal example

```yaml
remote_transmitter:
  id: ir_tx
  pin: GPIO13
  carrier_duty_percent: 50%
  non_blocking: true

climate:
  - platform: fujitsu_ac
    id: fujitsu_climate
    name: "AC"
    on_frame:
      then:
        - remote_transmitter.transmit_aeha:
            transmitter_id: ir_tx
            address: !lambda "return fujitsu_ir::ADDRESS_FUJITSU_IR;"
            data: !lambda "return x;"
            carrier_frequency: 38000Hz
```

### Full configuration reference

```yaml
climate:
  - platform: fujitsu_ac
    id: fujitsu_climate
    name: "AC"

    # Optional: ESPHome sensor entity ID providing current room temperature.
    # When provided, the climate entity will display current temperature.
    sensor_id: room_temp_sensor_id

    # Required: automation triggered when a frame is ready to transmit.
    # x is a std::vector<uint8_t> containing the complete AEHA payload.
    # Both 5-byte control frames (OFF, POWERFUL) and 14-byte full-state
    # frames are delivered through this trigger.
    on_frame:
      then:
        - remote_transmitter.transmit_aeha:
            transmitter_id: ir_tx
            address: !lambda "return fujitsu_ir::ADDRESS_FUJITSU_IR;"
            data: !lambda "return x;"
            carrier_frequency: 38000Hz
```

### Supported modes, fan speeds and swing

| Climate Mode | ESPHome Constant |
|---|---|
| Off | `CLIMATE_MODE_OFF` |
| Cool | `CLIMATE_MODE_COOL` |
| Heat | `CLIMATE_MODE_HEAT` |
| Dry | `CLIMATE_MODE_DRY` |
| Fan Only | `CLIMATE_MODE_FAN_ONLY` |
| Auto (Heat/Cool) | `CLIMATE_MODE_HEAT_COOL` |

| Fan Speed | ESPHome Constant |
|---|---|
| Auto | `CLIMATE_FAN_AUTO` |
| Low | `CLIMATE_FAN_LOW` |
| Medium | `CLIMATE_FAN_MEDIUM` |
| High | `CLIMATE_FAN_HIGH` |
| Quiet | `CLIMATE_FAN_QUIET` |

| Swing | ESPHome Constant |
|---|---|
| Off | `CLIMATE_SWING_OFF` |
| Vertical | `CLIMATE_SWING_VERTICAL` |

### Temperature limits

The physical remote enforces minimum target temperatures that vary by mode:

| Mode | Minimum | Maximum |
|---|---|---|
| HEAT | 16°C | 30°C |
| All other modes | 18°C | 30°C |

The component enforces these limits automatically. If a temperature below the minimum is requested, it is silently clamped to the minimum and Home Assistant is updated to reflect the actual value.

### Physical remote state tracking

If an IR receiver is present, the component can track state changes made via the physical remote by calling `update_from_ir()` from your `on_aeha:` automation. See the example file for a complete implementation including frame decoding.

### Powerful mode

The AR-REA1E remote includes a Powerful mode button that sends a 5-byte control frame. To expose this in Home Assistant, call `send_powerful_()` from a template button:

```yaml
button:
  - platform: template
    name: "AC Powerful"
    on_press:
      then:
        - lambda: 'id(fujitsu_climate).send_powerful_();'
```

`send_powerful_()` is a no-op when the unit is OFF.

---

## IR Protocol Documentation

This section documents the Fujitsu AR-REA1E IR protocol as determined by empirical analysis of captured frames. It is provided for reference and to assist others working with this hardware.

### Protocol basics

- **Modulation:** AEHA (Panasonic-style)
- **Carrier frequency:** 38 kHz
- **Address:** `0x28C6`
- **Frame header:** `0x00 0x08 0x08` (bytes [0–2], common to all frame types)

### Frame types

The AR-REA1E remote sends two frame types, distinguished by byte [3]:

| Frame type | Length | Byte [3] | Purpose |
|---|---|---|---|
| Control frame | 5 bytes | `0x40` (OFF), `0x9C` (POWERFUL) | Power off, Powerful toggle |
| Full-state frame | 14 bytes | `0x7F` | All other state changes |

### 5-byte control frame structure

```
Byte  Value     Description
[0]   0x00      Header
[1]   0x08      Header
[2]   0x08      Header
[3]   CMD       Command (0x40 = OFF, 0x9C = POWERFUL)
[4]   ~CMD      Checksum: bitwise NOT of byte [3]
```

### 14-byte full-state frame structure

```
Byte  Value     Description
[0]   0x00      Header
[1]   0x08      Header
[2]   0x08      Header
[3]   0x7F      Full-state command
[4]   0x90      Fixed (observed value, purpose unknown)
[5]   0x0C      Fixed (observed value, purpose unknown)
[6]   0x80|T    Power-on flag (0x80) OR'd with temperature nibble
[7]   M         Mode byte
[8]   F|S       Fan nibble (upper) OR'd with swing nibble (lower)
[9]   0xA8      Hour (not operationally significant)
[10]  0xE0      Fixed (observed value, purpose unknown)
[11]  0x42      Minute (not operationally significant)
[12]  0x04      Fixed (observed value, purpose unknown)
[13]  CS        Checksum (see below)
```

### Temperature encoding

Temperature is encoded as a 4-bit nibble in the lower half of byte [6]. The encoding is non-sequential:

| Temperature | Nibble | Temperature | Nibble |
|---|---|---|---|
| 16°C | `0x00` | 24°C | `0x01` |
| 17°C | `0x08` | 25°C | `0x09` |
| 18°C | `0x04` | 26°C | `0x05` |
| 19°C | `0x0C` | 27°C | `0x0D` |
| 20°C | `0x02` | 28°C | `0x03` |
| 21°C | `0x0A` | 29°C | `0x0B` |
| 22°C | `0x06` | 30°C | `0x07` |
| 23°C | `0x0E` | | |

### Mode encoding (byte [7])

| Mode | Byte value |
|---|---|
| Auto | `0x00` |
| Heat | `0x20` |
| Dry | `0x40` |
| Cool | `0x80` |
| Fan Only | `0xC0` |

### Fan encoding (byte [8] upper nibble, pre-shifted)

| Fan speed | Value |
|---|---|
| Auto | `0x00` |
| High | `0x80` |
| Medium | `0x40` |
| Low | `0xC0` |
| Quiet | `0x20` |

### Swing encoding (byte [8] lower nibble)

| Swing | Value |
|---|---|
| Off | `0x00` |
| Vertical | `0x08` |

### Full-state frame checksum algorithm

The checksum for 14-byte frames (byte [13]) is calculated as follows:

1. For each payload byte from index 6 to 12 (inclusive), reverse its bits
2. Sum all reversed bytes
3. Subtract the sum from the seed value `0xD0`
4. Reverse the bits of the result

```cpp
static inline uint8_t reverse_bits(uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) r |= ((b >> i) & 1) << (7 - i);
    return r;
}

uint8_t checksum_full(const uint8_t *buf) {
    uint8_t sum = 0;
    for (int i = 6; i <= 12; i++) sum += reverse_bits(buf[i]);
    return reverse_bits(0xD0 - sum);
}
```

The seed value `0xD0` was determined empirically for the AR-REA1E remote through analysis of captured frames. The checksum algorithm structure is based on George Dewar's work at https://gist.github.com/GeorgeDewar/11171561, adapted for this remote model.

The 5-byte control frame checksum is simply the bitwise NOT of byte [3]:
```cpp
uint8_t checksum_control(uint8_t cmd) {
    return (~cmd) & 0xFF;
}
```

---

## Contributing

Issues and pull requests are welcome, particularly for:
- Confirmation of compatibility with other ASTG\*KUCA models
- Identification of the purpose of fixed bytes [4], [5], [10] and [12]
- Support for additional swing modes if present on other variants

## License

MIT License — see [LICENSE](LICENSE) for details.
