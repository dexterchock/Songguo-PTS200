# Firmware Update Guide

## Option 1: Regular Update (USB Drag and Drop)

Use the update binary (`PTS200_v5.1.0_update.bin` or `firmware.bin`) when upgrading a working iron.

1. Plug the PTS200 into your computer with a USB data cable.
2. Hold the center button for 0.5s to 2.5s to open `Setup`, then select `Update FW`.
3. Wait for the `ESP32-FWMSC` USB drive to appear.
4. Drag and drop `PTS200_v5.1.0_update.bin` onto that drive.
5. The iron flashes the alternate OTA slot and reboots automatically once complete.

## Option 2: Factory Recovery Flash (`0x0000`)

Use the merged factory binary (`PTS200_v5.1.0_factory.bin`) if an update fails or you are flashing a blank ESP32 S2 module. This image includes the bootloader, partition table, OTA data, and firmware starting at address `0x0000`.

### Using esptool

1. Put the iron into USB download mode (hold the boot/center button while plugging in USB, or use 1200bps CDC touch).
2. Flash at offset `0x0000`:

```bash
esptool --chip esp32s2 --baud 921600 write_flash 0x0000 PTS200_v5.1.0_factory.bin
```

### Using ESP Flash Download Tool

1. Select chip `ESP32-S2` and load `PTS200_v5.1.0_factory.bin` at address `0x0000`.
2. Select `ESP32S2-Develop-Internal USB` and click Start.

Note: Upgrading from stock `v4.x` updates `VERSION_NUM` to `500`, resetting stored EEPROM parameters to defaults on first boot; upgrading from `v5.0.0` to `v5.1.0` keeps your saved settings and tip calibrations.
