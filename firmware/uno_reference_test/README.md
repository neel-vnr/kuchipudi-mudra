# UNO reference test for the 2.4" TFT shield

This is a **diagnostic**, not part of the MUDRA firmware. Its only job is to
answer one question: *is the panel good?*

The ESP32-S3 board has been unable to make the panel show pixels, while register
reads on the same bus are perfect (32/32 exact `93 41` ID reads, zero bit errors
on all 16 data lines). The one line that reads cannot test is **WR** (write
strobe), because reads never use it. Plugging the shield into an UNO removes
every jumper and every 3.3 V/5 V question at once.

## Steps

1. **Power off** the ESP32 and the external 5 V supply.
2. Unplug all 13 jumper wires from the shield.
3. Plug the shield **straight onto an Arduino UNO or MEGA 2560** — it seats on
   both the digital and the analogue headers. Nothing else is needed; the shield
   takes 5 V and GND from the UNO.
4. In the Arduino IDE install **MCUFRIEND_kbv** by David Prentice
   (Sketch → Include Library → Manage Libraries → search `MCUFRIEND_kbv`).
   It also pulls in Adafruit GFX, which it needs.
5. Open `uno_reference_test.ino`, select **Arduino UNO** and the UNO's COM port,
   and upload.
6. Open **Serial Monitor at 9600 baud**.

## What to report back

Two things:

1. The serial lines:
   ```
   readID   = 0x????
   driver   = 0x????
   size     = ???x???
   ```
   `0x9341` means the controller is ILI9341. `0x9325`, `0x5408` or `0x9320`
   mean the SPFD5408/ILI932x family. This finally settles which controller the
   panel really has.
2. What the panel shows: six vertical bars (black, red, green, blue, white,
   grey) plus `MUDRA UNO TEST` in the middle, cycling every 2 s.

## How to read the result

| Serial ID | Screen | Meaning |
|---|---|---|
| valid | colours + text | Panel is good. The fault is the ESP32 wiring — almost certainly the WR jumper or the shield pin it is plugged into. |
| valid | blank / white | Panel responds but does not render even on 5 V logic. Suspect the panel or its ribbon connector. |
| `0xD3D3` / `0xFFFF` / `0x0000` | blank | The shield is not answering at all on the UNO, which points at the shield itself. |

## If it works on the UNO

Then compare the UNO header pins the shield actually uses with the ESP32 map:

```
ESP32-S3   ->  shield pin
GPIO  4        RST
GPIO 10        CS
GPIO  9        RS / DC
GPIO  8        WR          <-- unverified, main suspect
GPIO  3        RD
GPIO 11..18    D0..D7
```

On the UNO the shield's own labels tell you which Arduino pin each signal goes
to (typically `CS = A3`, `CD/RS = A2`, `WR = A1`, `RD = A0`, `RESET = A4`, with
`D0..D7` on digital pins). If the shield's `WR` label is not the pin that the
GPIO 8 wire is attached to, that is the bug.
