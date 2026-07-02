# Orbix 6-Plug Hub

ESP32 Matter firmware for a 6-outlet plug controller with momentary switches and per-device factory QR commissioning.

Firmware lives in `orbix-6-plug-hub/`. See the [ESP-Matter docs](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html) for general build and flash instructions.

## GPIO pinout (Orbix ESP32)

| Function | GPIO |
|----------|------|
| Plug 1 (relay) | 21 |
| Plug 2 (relay) | 19 |
| Plug 3 (relay) | 18 |
| Plug 4 (relay) | 17 |
| Plug 5 (relay) | 16 |
| Plug 6 (relay) | 4 |
| Switch 1 (momentary) | 32 |
| Switch 2 (momentary) | 33 |
| Switch 3 (momentary) | 25 |
| Switch 4 (momentary) | 26 |
| Switch 5 (momentary) | 27 |
| Switch 6 (momentary) | 14 |
| Factory reset button | 34 |

Update pins via `idf.py menuconfig` → **Plugin manager**.

## Flashing with ESP32 DevKit

Wire the Orbix PCB to an ESP32 devkit for UART flashing. Connect **PCB → DevKit**:

| PCB | DevKit |
|-----|--------|
| Tx | Tx |
| Rx | Rx |
| GND | GND |
| GPIO0 | GND |
| 3.3V | 3.3V |
| EN | RST |

**Notes**

- **GPIO0 → GND** puts the ESP32 in download mode so `idf.py flash` and `esptool` can write firmware. Keep this jumper in place while flashing; remove it (or release GPIO0) before normal operation.
- **EN → RST** lets the devkit reset the PCB during flash.
- Use the devkit USB serial port when running `idf.py flash` or `flash_factory_device.py`. The flash script auto-detects the port if only one adapter is connected; otherwise pass `-p /dev/ttyACM0` or set `ESPPORT`.

## Manufacturing: QR codes and factory data

Each physical unit gets a unique Matter commissioning QR code and matching factory partition (DAC, passcode, discriminator). Device **#N** in manufacturing maps to folder `mfg_qr_codes/fff2_8001/N/`.

### Prerequisites

```bash
export ESP_MATTER_PATH=/path/to/esp-matter   # parent of connectedhomeip
source $IDF_PATH/export.sh                   # for esptool when flashing
```

Firmware `sdkconfig` must have factory commissioning enabled (already set in this project):

- `CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER=y`
- `CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL="fctry"`
- `CONFIG_DEVICE_VENDOR_ID=0xFFF2` / `CONFIG_DEVICE_PRODUCT_ID=0x8001`

Factory data flashes to the **`fctry`** partition at `0x3E0000`. Plug on/off state stays in the separate **`nvs`** partition and is not erased.

### Step 1 — Generate QR codes and factory partitions

From the repo root, generate 50 devices (change the count as needed):

```bash
./scripts/generate_orbix_qr.sh 50
```

This runs `esp-matter-mfg-tool` and reorganizes output into numbered folders:

```
mfg_qr_codes/fff2_8001/
├── 1/<uuid>/<uuid>-qrcode.png
├── 1/<uuid>/<uuid>-partition.bin
├── 2/<uuid>/...
...
└── 50/<uuid>/...
```

Print QR labels from `*-qrcode.png` (one per device number).

To reorganize manually after a standalone `esp-matter-mfg-tool` run:

```bash
python3 scripts/reorganize_mfg_output.py
```

### Step 2 — Flash factory data per device

Flash device **#5** (port is auto-detected if only one USB serial adapter is connected):

```bash
python3 scripts/flash_factory_device.py 5
```

Or specify the port explicitly:

```bash
python3 scripts/flash_factory_device.py 5 -p /dev/ttyACM0
```

Preview without writing:

```bash
python3 scripts/flash_factory_device.py 5 --dry-run
```

The script reads `orbix-6-plug-hub/sdkconfig` and `partitions.csv` for VID/PID and flash offset. It flashes `mfg_qr_codes/fff2_8001/<N>/*/*-partition.bin` to **`fctry` @ `0x3E0000`**.

Equivalent manual command:

```bash
esptool.py -p /dev/ttyACM0 write_flash 0x3E0000 \
  mfg_qr_codes/fff2_8001/5/*/*-partition.bin
```
### Step 3 — Build and flash firmware

```bash
cd orbix-6-plug-hub
source $IDF_PATH/export.sh
idf.py build flash monitor
```

Flash the application firmware once per batch before programming per-device factory data.

### Manufacturing line workflow

1. Flash application firmware (`idf.py flash`) on each board.
2. Run `flash_factory_device.py <N>` — **N** must match the QR label you attach.
3. Attach printed QR label **#N** from `mfg_qr_codes/fff2_8001/N/`.
4. Ship; customer commissions with that QR code.

Re-flashing factory data on a commissioned device changes credentials — the customer must pair again with the new QR code. Plug state is not affected (different partition).

## Plugin manager configuration

Three default on-off plugin units are shown in the upstream example; this project uses **6 plugs**. To change GPIO assignments:

1. `cd orbix-6-plug-hub && idf.py menuconfig`
2. Open **Plugin manager**
3. Update plug GPIO pins (use only pins available on your chip)

## Device performance

|                         | Bootup | After Commissioning |
|:------------------------|:------:|:-------------------:|
| **Free Internal Memory**| 212KB  | 127KB               |

**Flash usage:** firmware binary ~1.40MB (reference build on esp32c3_devkit_m).
