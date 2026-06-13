# Connect a device

The dongle hosts its own Wi-Fi access point. Any device with a modern
browser can use it — phones (iOS / Android), tablets, laptops, even a
desktop with a USB Wi-Fi dongle.

## Default credentials

| Field    | Value         |
|----------|---------------|
| SSID     | `BimmerzBox`  |
| Password | `bimmerzbox`  |
| IP       | `172.16.7.1`  |
| Channel  | 6 (2.4 GHz)   |

Change them in [`/settings/`](#changing-the-wi-fi-credentials) — strongly
recommended once you've confirmed the dongle works.

## The captive portal

When you join the dongle's AP for the first time, your OS detects
that the network has no internet and pops a **captive-portal**
window: a sandboxed browser running our welcome page. You see a
splash with the bimmerz box brand and one **OK, got it** button.

What happens when you tap it:

- The dongle records that your client has acknowledged the portal.
- The portal screen updates to "You can dismiss this window".
- On most platforms the OS closes the sheet automatically within a
  few seconds. On macOS you may need to tap **Cancel** in the
  corner — it just dismisses the window, you stay connected.

You can now open your normal browser app and navigate to
`http://172.16.7.1/`.

::: tip Why not just auto-open the dashboard?
The captive portal runs in a sandboxed mini-browser that the OS
controls. It doesn't have access to your real browser's cookies,
extensions, or persistent state — and on iOS especially it can't
reliably hand a URL off to Safari. So instead of a fragile
"automatic" handoff, the welcome screen just tells you the address
and gets out of the way.
:::

## Connecting on iOS

1. Settings → Wi-Fi → tap **BimmerzBox**.
2. Enter the password if prompted.
3. The "Sign In" sheet pops up — that's the captive portal.
4. Tap **OK, got it**, then **Cancel** in the top-left.
5. Open Safari → `http://172.16.7.1/`.

iOS may keep showing a "No Internet Connection" warning on the
BimmerzBox network. That's expected — the dongle is intentionally
not connected to the internet. The warning doesn't stop the
connection from working.

## Connecting on Android

1. Wi-Fi settings → tap **BimmerzBox**.
2. Enter the password.
3. The captive notification pops; tap it.
4. Tap **OK, got it** in the welcome screen.
5. Open Chrome / Firefox → `http://172.16.7.1/`.

Android will warn that the network has no internet. Confirm "Stay
connected" or similar — the warning is correct but harmless.

## Connecting on macOS / Windows / Linux

1. Wi-Fi menu → connect to **BimmerzBox**.
2. The captive-portal sheet appears (macOS) or notification (Win/Linux).
3. Click **OK, got it** in the welcome screen.
4. Dismiss the sheet (the **Cancel** button on macOS, or just close
   the notification).
5. Open any browser → `http://172.16.7.1/`.

## Cable side — OBD-II connection

The dongle ships with a standard 16-pin OBD-II connector (or, for the
DIY build, you fit one). For pre-OBD-II cars (E30 / E34 / E36 with
the round 20-pin diagnostic plug under the bonnet), you'll need a
20-pin-to-OBD-II adapter cable. They're cheap and widely available.

Pin mapping the dongle uses:

| OBD-II pin | Signal     | Note                                 |
|------------|------------|--------------------------------------|
| 4          | Chassis GND|                                       |
| 5          | Signal GND |                                       |
| 6          | CAN-H      | HS-CAN (PT-CAN, 500 kbit/s)          |
| 7          | K-line     | ISO 9141 / KWP2000 / DS2             |
| 8          | L-line     | Output-only on dongle                |
| 14         | CAN-L      | HS-CAN                               |
| 16         | +12 V batt | Permanent battery feed (powers box)  |

On bench: power the cable from any 12 V source between pin 16 and
pin 4 (or pin 5).

## Changing the Wi-Fi credentials

1. Connect, open `http://172.16.7.1/settings/`.
2. Under **Wi-Fi**, edit SSID / password / channel.
3. Click **Save**, then **Restart**.
4. Reconnect with the new credentials.

Forgot the password? Hold the multi-purpose button on the dongle for
5 seconds at boot to factory-reset NVS. The defaults come back.
