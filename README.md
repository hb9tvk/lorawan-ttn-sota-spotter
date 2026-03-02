# LoRaWAN TTN SOTA Spotter

A handheld [SOTA](https://www.sota.org.uk/) (Summits on the Air) spotting device based on the
[Heltec Wireless Tracker](https://heltec.org/project/wireless-tracker/) (ESP32-S3 + SX1262 LoRa + GPS).
The operator selects band, mode and message on a small TFT display; pressing the button transmits a
16-byte LoRaWAN uplink via [The Things Network](https://www.thethingsnetwork.org/) to an AWS Lambda
function that posts the spot to [SOTAWatch3](https://sotawatch.sota.org.uk/).

## Hardware

| Component | Details |
|---|---|
| MCU | ESP32-S3 |
| Radio | SX1262 (LoRa, 868 MHz EU band) |
| GPS | on-board UART GPS (115200 baud) |
| Display | ST7735 80×160 TFT, SPI |
| Input | 5-way joystick (GPIO 0/4/5/6/7) |

## System Architecture

```
[Device] --LoRaWAN OTAA--> [TTN] --HTTP webhook--> [AWS Lambda] --> [SOTAWatch3 API]
```

1. On power-up the device joins TTN via OTAA (up to 10 attempts).
2. GPS acquires a fix; the nearest Swiss SOTA summit is identified.
3. The operator adjusts band, frequency, mode and message via the joystick.
4. Pressing the centre button encodes the spot into a 16-byte binary payload and transmits it.
5. TTN delivers the raw payload to the Lambda webhook.
6. Lambda decodes the binary struct, authenticates with the SOTA SSO, and posts the spot.

## Payload Format

The uplink is a C packed struct (`__attribute__((packed))`), 16 bytes:

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0 | 11 | `char[11]` | `ref` | Summit reference, null-padded (e.g. `HB/LU-001`) |
| 11 | 4 | `float` (LE) | `qrg` | Frequency in **kHz** |
| 15 | 1 | `uint8_t` | `modmsg` | Low nibble = mode index, high nibble = message index |

Mode indices: `0`=CW, `1`=SSB, `2`=FM, `3`=DATA
Message indices: `0`=QRV now, `1`=QRT, `2`=TEST

A `TEST` spot is posted with `"type": "TEST"` so SOTAWatch3 marks it accordingly.

## User Interface

The TFT shows five lines, navigated with Up/Down. Left/Right adjusts the selected field.
The centre button sends the spot.

```
HB/LU-001              ← nearest summit (read-only)
40m              234m  ← band (select) · distance to summit (right-aligned)
7032.00                ← frequency in kHz (fine-tune ±0.5 kHz)
CW                     ← mode (CW / SSB / FM / DATA)
QRV now                ← message (QRV now / QRT / TEST)
```

The selected line is highlighted in red at double size. After sending, the screen turns green
(spot sent) or red (TTN error) for three seconds, then returns to the menu.

## Setup

### 1. PlatformIO firmware

```bash
cd firmware   # project root
pio run -t upload
```

**LoRaWAN credentials** must be placed in `secrets.ini` (gitignored) in the project root:

```ini
[secrets]
lorawan_flags =
    -DRADIOLIB_LORAWAN_JOIN_EUI=0xAABBCCDDEEFF0011
    -DRADIOLIB_LORAWAN_DEV_EUI=0xAABBCCDDEEFF0022
    '-DRADIOLIB_LORAWAN_APP_KEY=0xAA, 0xBB, ..., 0xFF'
    '-DRADIOLIB_LORAWAN_NWK_KEY=0xAA, 0xBB, ..., 0xFF'
```

These match the OTAA credentials from your TTN application. The `platformio.ini` loads this file
via `extra_configs = secrets.ini`.

> **First upload on a new ESP32-S3:** Hold BOOT, tap RESET, then run `pio run -t upload`.
> Subsequent uploads work without manual intervention.

### 2. TTN application

- Create an application in TTN and register the device with OTAA (EU868).
- Under **Integrations → Webhooks**, add a webhook pointing to your Lambda function URL.
  - Base URL: your API Gateway URL
  - Enabled messages: **Uplink message**
- No payload formatter is needed — Lambda decodes the binary `frm_payload` directly.

### 3. AWS Lambda

Deploy `aws/sotaspot/lambda_function.py` (Python 3.x, `requests` layer required).

Set the following environment variables:

| Variable | Description |
|---|---|
| `SOTA_USERNAME` | Your SOTA SSO username |
| `SOTA_PASSWORD` | Your SOTA SSO password |
| `SOTA_CALLSIGN` | Your callsign (default: `HB9TVK`) |

The function uses a three-level auth strategy to minimise full logins:
1. **Module-level cache** — reuses the access token across warm Lambda invocations.
2. **S3 refresh token** — stored in bucket `hb9tvk-sotaspot`, key `refresh.txt`; valid ~30 days.
3. **Full SSO login** — browser-simulated PKCE flow, runs only when the refresh token has expired.

Create the S3 bucket and grant the Lambda execution role `s3:GetObject` / `s3:PutObject` on it.
The refresh token is seeded automatically on the first successful login.

## Summit Database

`summitslist.csv` is the full SOTA summit list (downloaded from
[database.sota.org.uk](https://database.sota.org.uk/)).
`s2h.py` filters it to currently-valid Swiss summits (`HB/*`) and generates `src/summits.h`:

```bash
python3 s2h.py > src/summits.h
```

Re-run this whenever new Swiss summits are added to the SOTA database.

## Dependencies (PlatformIO)

| Library | Version |
|---|---|
| [RadioLib](https://github.com/jgromes/RadioLib) | ^7.1.0 |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | 1.0.3 |
| [OneButton](https://github.com/mathertel/OneButton) | 1.5.0 |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | 2.5.43 |

## License

MIT
