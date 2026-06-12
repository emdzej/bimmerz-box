# Troubleshooting

When the box won't behave, walk down this list in order. Most
problems are in the first three rows.

## Dongle doesn't power up

**Symptoms:** no LED, the Wi-Fi network never appears.

- Check the OBD-II cable seating — the connector needs to be fully
  home. Some OBD-II receptacles are recessed behind a trim flap;
  the dongle has to push past it.
- Verify pin 16 (battery +12 V) on the car-side connector with a
  multimeter. Should read 11.5–13 V regardless of ignition state on
  every BMW from E36 onward.
- Some cars (e.g. E60 / E83 with very low SOC) drop pin 16 below
  the dongle's brownout threshold (~8 V). Charge the car battery
  or jump-start before retry.
- DIY build: check the 12 V → 3.3 V buck is producing 3.3 V on
  TP_3V3 (probe pad near the regulator). Most likely culprit is a
  cold solder joint on the inductor or a reversed input cap.

## Wi-Fi network doesn't appear

**Symptoms:** dongle powers up (LED on) but `BimmerzBox` isn't in
your Wi-Fi list.

- Wait 8 seconds after power-on. The ESP32-C6 co-processor needs to
  initialise via SDIO before the AP comes up.
- Restart Wi-Fi scanning on your device. Some phones cache results.
- If you've previously joined and changed credentials, your device
  may be remembering an old SSID. "Forget this network" and rescan.
- If still missing: connect the dongle to a computer via USB, run
  `idf.py monitor` and look for `wifi_ap: AP up on SSID
  "BimmerzBox"` in the boot log. No such line → the C6 link failed
  to come up. See [ESP-Hosted version pinning](#esp-hosted-issues)
  below.

## Captive portal doesn't show

**Symptoms:** Wi-Fi connects but no welcome screen pops up.

- Open a browser manually and visit
  [`http://172.16.7.1/welcome`](http://172.16.7.1/welcome). If it
  loads, tap **OK, got it**, then **Cancel** the OS sheet — same
  effect.
- On some Android versions the captive notification is silent. Pull
  down the notification shade — there's usually a "Sign in to
  network" entry.
- iOS occasionally caches a previous portal-accepted state. Forget
  the network and re-join.

## ECU doesn't respond

**Symptoms:** EDIABASX loads, you pick an SGBD and a job, hit Run,
and the request times out or returns `EDXN_ERR_TRANSPORT` (code 8).

- **Ignition.** Most ECUs only respond with ignition in position 2.
  Some only respond when the engine is running. Cycle the key.
- **Wrong SGBD.** Different chassis / model years use different
  variants. If `MS420DS0` doesn't respond, try `MS430DS0`,
  `MSS54DS0`, etc. The group file (`d_motor.grp`) auto-resolves
  for engine ECUs.
- **K-line vs CAN.** Older ECUs (E36/E38/E39 and most E46 non-M)
  are K-line at 9600 8E1 DS2. E46 M3 / E60 / E83 and later are
  often K-line KWP2000 at 10400 8N1 or CAN. The SGBD knows which —
  the dongle picks it up automatically.
- **`/rpc/uart/0` held by another app.** If a flasher or sniffer
  has claimed the K-line directly, EDIABASX returns
  `EDXN_ERR_TRANSPORT` cleanly rather than stomping the bus.
  Close the other tab.

## Apps load but show "Server disconnected"

**Symptoms:** dashboard appears, but EDIABASX / INPAX show a "WS
disconnected" indicator.

- The HTTP server is up (dashboard renders) but the WebSocket
  upgrade failed. Refresh the page — most browsers retry the
  upgrade on reload.
- If a refresh doesn't fix it: visit `/admin/` → **Restart**.
  Common cause is the dongle's heap fragmenting after a long
  uptime; a restart clears it.

## Getting logs out

When you're stuck, the dongle's serial console is the source of
truth. Connect a USB cable, then on your computer:

```sh
# ESP-IDF toolchain installed
idf.py -p /dev/cu.usbmodem... monitor

# Or any serial terminal
screen /dev/cu.usbmodem... 115200
```

Hit **RESET** on the dongle. You'll see the full boot sequence and
any later runtime warnings. Copy-paste the relevant lines into a
GitHub issue.

## ESP-Hosted issues

Symptoms: C6 link fails, no Wi-Fi.

The firmware pins `espressif/esp_hosted` to `^1.4.0` deliberately —
2.x crashed on the Waveshare DEV-KIT with a NULL queue assertion at
boot. If you've manually bumped the version, revert to 1.4.x.

## More help

- File a bug:
  [github.com/emdzej/bimmerz-box/issues](https://github.com/emdzej/bimmerz-box/issues).
  Include the boot log and what you were doing when it broke.
- Hardware questions on the DIY path:
  [github.com/emdzej/bimmerz-box/discussions](https://github.com/emdzej/bimmerz-box/discussions).
