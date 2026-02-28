# ajazz-ak35i-v3-max

![Ajazz AK35I V3 MAX](logo.png)

Linux command-line tool for the **Ajazz AK35I V3 MAX** keyboard.

Controls the 1.14-inch TFT display (time sync, image and animation upload)
and the RGB key lighting, without requiring root access.

> **Disclaimer:** This is an unofficial, community-written tool with no
> affiliation to Ajazz or any other manufacturer. It has been developed and
> tested on the model named above, but is provided **as-is**, without warranty
> of any kind. Use it at your own risk. The authors accept no responsibility
> for damage to your keyboard, loss of data, or any other harm resulting from
> its use.

## Features

| Command | Description |
|---------|-------------|
| `list` | List all detected HID interfaces |
| `time` | Sync the display clock to system time |
| `light` | Set RGB lighting mode and colour |
| `solid` | Fill the display with a solid colour |
| `image` | Upload a static image (PNG, JPG, BMP, GIF) |
| `animation` | Upload an animated GIF (up to 141 frames) |

## Requirements

- Linux
- C++17 compiler (GCC 8+ or Clang 7+)
- CMake 3.16+

## Physical setup

On the back of the keyboard, set the connection switch to **USB**.
The OS switch (Win/MAC) does not affect the protocol.

## Build and install

### Option A — Debian/Ubuntu package (recommended)

```bash
make package
sudo dpkg -i build/ajazz-ak35i-0.2.0-Linux.deb
```

This installs the binary to `/usr/bin/ajazz` and the udev rule to
`/etc/udev/rules.d/`, and reloads udev automatically via the package
post-install script. To remove:

```bash
sudo dpkg -r ajazz-ak35i
```

### Option B — Manual install

```bash
make
make install
```

Installs the binary to `/usr/local/bin` and the udev rule to
`/etc/udev/rules.d/`, then reloads udev. To install to a different prefix:

```bash
make install PREFIX=~/.local
```

To uninstall:

```bash
make uninstall
```

## Device permissions (udev)

On a stock Ubuntu system, `/dev/hidrawN` nodes are not writable by regular
users. `make install` handles this automatically by installing
`udev/99-ajazz-ak35i.rules`.

No re-plug or reboot is needed — `udevadm trigger` applies the rules to
already-connected devices immediately.

## Usage

```
ajazz [--verbose] [--quiet] <command> [options]
```

### list

```bash
ajazz list
```

Prints all HID interfaces found for the keyboard (VID=0x0C45, PID=0x8009).
Useful for verifying the device is detected.

### time

Syncs the on-board RTC to local system time. After one sync the display clock
runs autonomously — no daemon or repeated calls needed.

```bash
ajazz time                              # sync to current system time
ajazz time --at 14:30:00               # set time to 14:30 today
ajazz time --at 2026-06-01T09:00:00    # set full date and time
```

The device stores time as-is with no timezone information. Set it to your
local time.

### light

Sets the RGB key backlight. Changes are saved to flash and survive power cycles.

```bash
ajazz light                                    # solid white (default)
ajazz light -m static -r 255 -g 0 -b 0        # solid red
ajazz light -m breath -r 0 -g 128 -b 255      # blue breathing
ajazz light -m breath --brightness 3          # breathing at medium brightness
ajazz light -m spectrum --rainbow             # full colour cycle
ajazz light -m falling --speed 5 --direction 1
ajazz light -m off                            # backlight off
```

**Available modes:**
`off`, `static`, `singleon`, `singleoff`, `glitter`, `falling`, `colourful`,
`breath`, `spectrum`, `outward`, `scrolling`, `rolling`, `rotating`, `explode`,
`launch`, `ripples`, `flowing`, `pulsating`, `tilt`, `shuttle`

**Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `-m, --mode` | `static` | Mode name or hex value `0x00`–`0x13` |
| `-r, --red` | `255` | Red 0–255 |
| `-g, --green` | `255` | Green 0–255 |
| `-b, --blue` | `255` | Blue 0–255 |
| `--rainbow` | off | Multicolour cycling (overrides RGB) |
| `--brightness` | `5` | 0 (off) – 5 (full) |
| `--speed` | `3` | 0 (slow) – 5 (fast) |
| `--direction` | `0` | 0=left, 1=down, 2=up, 3=right |

### solid

Fills the 240×135 display with a single colour.

```bash
ajazz solid                          # solid red (default)
ajazz solid -r 0 -g 255 -b 0        # solid green
ajazz solid -r 0 -g 0 -b 0          # black / clear
```

### image

Uploads a static image to the display. The image is scaled to 240×135.

```bash
ajazz image photo.jpg                # letterbox fit (default)
ajazz image logo.png --fill          # scale-to-fill, centre-crop
ajazz image frame.gif                # first frame of a GIF
```

**Accepted formats:** PNG, JPG, BMP, GIF (first frame only), or a
pre-converted `.raw` file (256-byte header + RGB565-LE pixel data).

**Fit modes:**

- Default (letterbox): the image is scaled to fit within 240×135, preserving
  aspect ratio, with black bars on the shorter sides.
- `--fill`: the image is scaled to cover 240×135 and centre-cropped.
  No black bars; edges may be clipped.

### animation

Uploads an animated GIF to the display. Capped at 141 frames (firmware limit).

```bash
ajazz animation anim.gif             # upload animated GIF
ajazz animation anim.gif --fill      # scale-to-fill
ajazz animation pre-converted.raw    # raw file with header
```

Upload progress is shown as a progress bar. A 141-frame animation takes
approximately 78 seconds to upload (~35 ms per 4 KB chunk).

**Frame delays** are stored as 1-byte values in 2 ms units (range: 2–510 ms).
GIF delays outside this range are clamped.

## Dependencies

| Dependency | Version | Source |
|------------|---------|--------|
| [stb_image](https://github.com/nothings/stb) | vendored | `dependencies/stb/` |
| [stb_image_resize2](https://github.com/nothings/stb) | vendored | `dependencies/stb/` |

## Contributing

Pull requests are welcome — including AI-generated ones. This is a spare-time
project, so please be patient: it may take weeks or months before a PR gets
reviewed. Bug reports and protocol findings are equally appreciated.

## Protocol

See [PROTOCOL.md](PROTOCOL.md) for the HID command protocol reference.
