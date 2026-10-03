# Invectus DUCO CYD Miner

Target board: **ESP32-2432S024C**, 2.4-inch 240x320 ILI9341 LCD with CST820 capacitive touch.

## What this firmware does

- Runs DUCO-S1 mining on both ESP32 cores using the same job format and ESP32 difficulty tier as Duino-Coin 4.3.
- Uses a first-boot captive portal for Wi-Fi, Duino-Coin username, optional mining key and rig name.
- Stores configuration in ESP32 NVS; credentials are not compiled into the firmware.
- 320x240 landscape touch UI with Miner, Account, Network and Settings pages.
- Shows live total hashrate, accepted/rejected shares, block responses, difficulty, ping, node, uptime, Wi-Fi RSSI and account balance.
- Long-press the red reset button on Settings for 2.5 seconds to erase Wi-Fi + DUCO profile and reboot into setup mode.

## First boot

1. Flash the merged image at address 0x0000.
2. Reboot.
3. Connect a phone/laptop to the Wi-Fi network shown on screen: INVECTUS-DUCO-XXXX.
4. Setup AP password: invectus
5. Enter your home Wi-Fi, Duino-Coin username, mining key only if enabled, and optional rig name.
6. Save and allow the ESP32 to reboot/connect.

BlueWallet is a Bitcoin wallet and is not used for DUCO mining. DUCO mining credits the Duino-Coin account username configured in the setup portal.

## Hardware mapping

- ILI9341: SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, BL 27
- CST820: SDA 33, SCL 32, RST 25, INT 21
- Display rotation: 3 (landscape 320x240)

## Upstream / license note

The DUCO-S1 algorithm and wire protocol behavior are derived from the official Duino-Coin ESP miner v4.3 (MIT). The display, setup and dashboard integration in this folder are Invectus-specific. This build is not endorsed by the Duino-Coin project.

Before using a custom build on a valuable account, review the current Duino-Coin Terms of Service. The project warns that outdated/unofficial miners can receive warnings or bans. This firmware deliberately uses the ESP32 difficulty tier and current 4.3 protocol behavior and does not attempt to bypass mining limits.
