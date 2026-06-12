# Quick start

From "in my hand" to "reading fault codes" in five steps. Roughly 3
minutes if it's your first time.

## 1. Plug into the OBD-II port

The diagnostic port on a BMW lives:

- **E36 / E34** — under the bonnet on the right inner wing (round 20-pin
  socket). You'll need a 20-pin-to-OBD-II adapter cable.
- **E38 / E39 / E46** — driver's footwell, usually behind a small flap
  on the lower dash trim. Standard 16-pin OBD-II.
- **E60 / E83 / E90 and later** — driver's footwell, OBD-II.

Ignition to position 2 (or engine running). The dongle's status LED
should pulse blue.

::: tip Power expectations
The box runs off the OBD-II port's permanent 12 V (pin 16). It powers
on the moment the cable is plugged in — ignition doesn't gate the
dongle itself, only what the ECUs will respond to.
:::

## 2. Join the dongle's Wi-Fi

On your phone, tablet, or laptop:

- **SSID:** `BimmerzBox`
- **Password:** `bimmerzbox` *(change it after first connect — see
  [`/admin/`](#) below)*

The dongle is an **AP-only** device. It doesn't connect to your home
Wi-Fi or share internet. Your device temporarily drops off its usual
network while you're connected to the box — this is normal.

## 3. Tap **OK** in the welcome screen

Modern phones and laptops detect that the Wi-Fi has no internet and
pop a **captive-portal** window. You'll see a welcome screen with the
bimmerz box logo and a single **OK, got it** button.

Tap it. The screen updates with a "you can dismiss this window" hint
— either it closes by itself within a few seconds, or you tap
**Cancel** in the corner to dismiss (don't worry — you stay connected
to the dongle).

## 4. Open the dashboard

In your browser, go to:

```
http://172.16.7.1/
```

You should see the bimmerz hub — a grid of tiles for each installed
app. Tiles are dimmed for apps that aren't yet uploaded to the SD card.

## 5. Pick a tool

- **EDIABASX** — diagnostic jobs. Browse the SGBD catalogue, pick an
  ECU, run jobs like `IDENT`, `STATUS_MESSWERTBLOCK_LESEN`,
  `FS_LESEN` (fault codes).
- **INPAX** — live values. The BMW dealer-tool experience, in a
  browser.
- **NCSX** — coding. Plain-English option labels, tick the box, write
  the change back to the ECU.
- **NFSX** — flashing. DS2 direct-mode and Bosch C167 bootmode paths.
- **TUNEX** — edit ECU firmware images.

::: warning Read first, write second
Reading from your car is safe. **Writing** — coding changes, fault
clears, flashing — can brick an ECU if you do it wrong. Always read
the per-tool guide first and back up before you write.
:::

## Next steps

- **[User guide](/guide/)** — deeper walkthrough of each tool, the
  admin page, and the file browser.
- **[Troubleshooting](/guide/troubleshooting)** — when the box won't
  power on, the captive portal won't appear, or the ECU doesn't
  respond.
- **[Updating firmware](/guide/firmware)** — push a new firmware
  build via the dongle's OTA mechanism.
