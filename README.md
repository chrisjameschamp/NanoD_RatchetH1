# NanoD_RatchetH1 Firmware

Custom firmware for the Nano_D++ and Ratchet_H1 haptic MIDI controllers, forked from [katbinaris/NanoD_RatchetH1](https://github.com/katbinaris/NanoD_RatchetH1).

For an easy-to-use CLI tool to configure profiles, see [nanod-cli](https://github.com/betodealmeida/nanod-cli).

## Features

This fork adds several enhancements to the original firmware:

### Pitchwheel Mode

A new haptic mode (mode 4) that provides spring-loaded pitch bend control:

- Spring-loaded feel that returns to center when released
- 14-bit MIDI pitch bend output (-8192 to +8191)
- Configurable deflection range and spring strength
- Smooth, expressive control for pitch bending

### Per-Profile Position Memory

When switching between profiles, the firmware now remembers each profile's knob position. Switching from Profile A to Profile B and back to Profile A will restore Profile A's last position, rather than resetting to the start position.

### Per-KeyState Position Memory

Within each profile, different key states (button combinations) can control different parameters. The firmware remembers each key state's position independently:

- Turn the knob normally to adjust Parameter 1
- Hold Button A and turn to adjust Parameter 2
- Release Button A - Parameter 1's position is preserved
- Hold Button A again - Parameter 2's position is preserved

This allows a single profile to act as multiple "pages" of controls while maintaining state across all of them.

### LCD Parameter Display

Each knob configuration can have a `desc` field that displays on the LCD when that key state is active. This provides clear visual feedback about which parameter you're currently controlling.

## Building

This project uses PlatformIO. To build and upload:

```bash
# Build only
pio run -e nanofoc_d

# Build and upload
pio run -e nanofoc_d -t upload
```

## Configuration

The device is configured via JSON messages over USB serial. See [communications.md](communications.md) for the full protocol specification.

For a more user-friendly configuration experience, use the [nanod-cli](https://github.com/betodealmeida/nanod-cli) tool which supports YAML presets:

```bash
pip install -e path/to/nanod-cli
nanod upload preset.yaml --activate --save
```

## Protocol Documentation

- [communications.md](communications.md) - Serial JSON protocol
- [midi.md](midi.md) - MIDI output protocol
- [hid.md](hid.md) - USB HID protocol

## Hardware

- ESP32-S3 microcontroller
- SimpleFOC motor control
- TFT LCD display (LVGL)
- 4 programmable buttons
- RGB LED ring
- USB-C with MIDI, HID, and Serial

## Differences From the Original Firmware

This updated firmware keeps the original NanoD_RatchetH1 behavior and adds support for using the Nano_D++ as an external fader controller with connected desktop applications.

- Adds `{"level": <position>}` serial command so connected applications can set the knob position.
- Adds an `isFader` profile flag that is saved to profile JSON and included when profiles are exported over serial.
- Displays fader profiles as dB values on the Nano screen instead of raw knob positions.
- Maps fader display values from `-60.0 dB` to `+12.0 dB`.
- Updates the value screen accent color from orange to blue.
- Updates the large digit font glyphs to better support decimal and negative dB readouts.
- Fixes the profile action parser comparison from `type="profiles"` to `type=="profiles"`.
- Updates idle LED animation handling to write the main LED ring and button LEDs separately.

## License

This project is licensed under the **GNU General Public License v3.0** (GPL-3.0). See [LICENSE](LICENSE) for details.

This is required because the project includes the XTI2S audio library which is GPL-3.0 licensed.
