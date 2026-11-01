# Vivobook Fan Control

Fan control and monitoring for the **ASUS Vivobook 14X M7400QE / M7400QC** on Linux —
a Qt 6 graphical window and a `fanctl`-style command line, packaged as a `.deb`.

![Vivobook Fan Control GUI](docs/screenshot.png)

The M7400QE (Ryzen 9 7940HS / RTX 3050) exposes its fans through the kernel's `asus`
hwmon driver, which on this model offers only a **control mode** per fan — firmware
automatic, or forced full speed. This tool reads both fans plus their temperatures and
lets you switch between those two modes from the GUI or the shell.

- **Tested on:** Vivobook 14X M7400QE, Pop!_OS 24.04 (COSMIC, Wayland), kernel 7.2.x
- **Expected to work on:** any Vivobook whose `asus` hwmon device exposes `pwm1_enable`
  / `pwm2_enable` with `cpu_fan` / `gpu_fan` labels — which includes the M7400QC.
  It is not limited to these two models; device discovery happens at runtime.

## Features

| | |
|---|---|
| Live readout | CPU and GPU fan RPM, plus CPU (`k10temp`) and GPU (`amdgpu`) temperatures, refreshed every 2 s |
| Mode control | Switch each fan independently between **Automatic** and **Full speed** |
| History graphs | 5-minute rolling charts for temperature and fan speed, painted with `QPainter` (no Qt Charts dependency) |
| Tray menu | Quick "all fans automatic / full speed" actions, with live RPM in the tooltip |
| Restore at boot | Optionally re-apply your chosen modes early in the next boot via a systemd unit |
| CLI parity | Every GUI action is also available on the command line, sharing one codebase |
| No sudo rules | Writes go through a polkit-authorized root helper; sysfs stays root-owned |

## Install the .deb

Download `vivobook-fan-control_1.0.0_amd64.deb` from the
[latest release](https://github.com/WILBOARDS/vivobook-fan-control/releases/latest),
then:

```bash
# 1. Install (pulls in the Qt 6 runtime libraries automatically)
sudo dpkg -i vivobook-fan-control_1.0.0_amd64.deb

# 2. Fix up dependencies if dpkg reported any
sudo apt --fix-broken install

# 3. Confirm the controller was detected
vivobook-fanctl status
```

Then launch **Vivobook Fan Control** from your application menu, or run
`vivobook-fan-control` from a terminal. To remove it: `sudo apt remove vivobook-fan-control`.

## Build from source

```bash
# 1. Build dependencies (Ubuntu 24.04 / Pop!_OS 24.04)
sudo apt install cmake ninja-build g++ qt6-base-dev dpkg-dev

# 2. Clone and build
git clone https://github.com/WILBOARDS/vivobook-fan-control.git
cd vivobook-fan-control
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 3. Run straight from the build tree (fan changes still need the installed
#    polkit policy, so status works here but mode changes will be refused)
./build/vivobook-fan-control status

# 4. Produce the .deb — no debhelper or root required
./scripts/build-deb.sh
#    -> debian/out/vivobook-fan-control_1.0.0_amd64.deb

# 5. Install it
sudo dpkg -i debian/out/vivobook-fan-control_1.0.0_amd64.deb
```

## Command line

```
vivobook-fanctl status                Show fan modes, RPM and temperatures
vivobook-fanctl cpu auto|full         Set the CPU fan mode
vivobook-fanctl gpu auto|full         Set the GPU fan mode
vivobook-fanctl all-auto              Restore both fans to automatic
vivobook-fanctl all-full              Run both fans at full speed
vivobook-fanctl boot on|off           Re-apply the chosen modes at boot
vivobook-fanctl gui                   Open the graphical window
```

Example:

```
$ vivobook-fanctl all-full
cpu fan set to full
gpu fan set to full

$ vivobook-fanctl status
=== ASUS Vivobook Fan Status ===
CPU Fan: 6100 RPM | Full speed
GPU Fan: 2400 RPM | Full speed
CPU Temp: 46.5 C
GPU Temp: 41.0 C

Hardware path: /sys/class/hwmon/hwmon6
Restore at boot: disabled
```

> **Why not `fanctl`?** Pop!_OS ships `/usr/sbin/fanctl` from the `ubuntu-fan` package,
> and `/usr/sbin` usually precedes `/usr/bin` in `PATH`. The command is therefore named
> `vivobook-fanctl` to avoid shadowing it.

## Graphical interface

Each fan gets a card showing RPM, temperature, the mode currently reported by the
firmware, and Automatic / Full speed buttons. The two graphs below keep the last five
minutes of samples.

- **Blast both fans** forces both to full speed; **Return to automatic** hands control
  back to the firmware. Use the second one when you are done — full speed is meant for
  quick cooldown or diagnosing a fan, not as a permanent setting.
- **Restore these fan modes at boot** enables `vivobook-fan-restore.service`, which
  re-applies whatever modes you last chose.
- Closing the window quits. The tray menu (when the desktop provides a StatusNotifier
  host) offers the same actions without opening the window.

## How it works

The app itself never writes to sysfs. It is split in three:

```
vivobook-fan-control  ──GUI + CLI, unprivileged, reads /sys/class/hwmon──┐
                                                                        │ pkexec
fancontrol-helper     ──root, validates args, writes pwmN_enable────────┘
        ▲
org.vivobookfancontrol.policy  ──polkit action, auth_admin_keep
```

| Path | Role |
|---|---|
| `/usr/bin/vivobook-fan-control` | GUI and CLI binary |
| `/usr/bin/vivobook-fanctl` | Symlink used for command line invocation |
| `/usr/libexec/vivobook-fan-control/fancontrol-helper` | Root helper: `apply`, `restore`, `boot` |
| `/usr/share/polkit-1/actions/org.vivobookfancontrol.policy` | Authorization rules for the helper |
| `/usr/lib/systemd/system/vivobook-fan-restore.service` | One-shot unit that re-applies modes at boot |
| `/var/lib/vivobook-fan-control/modes` | The modes you last chose, e.g. `cpu=auto` |

The helper discovers the hwmon device itself rather than accepting a path from the
caller, validates every argument against a fixed vocabulary, never spawns a shell, and
reads the value back to confirm the firmware accepted it.

`pwmN_enable` values on this machine: `0` = full speed, `2` = automatic. The driver
exposes no `pwmN` value file, so classic manual PWM curves (`1`) are not available —
firmware control is the ceiling here, not a limitation of this tool.

## Troubleshooting

**"ASUS fan controller not found under /sys/class/hwmon"**
The platform driver is not loaded. Check `lsmod | grep asus` and confirm a chip named
`asus` exists: `for d in /sys/class/hwmon/hwmon*; do echo "$d $(cat $d/name)"; done`.

**The authentication dialog is rejected, or the app hangs on "Waiting for authentication"**
This package asks for `auth_admin` (cached for the session). On COSMIC, an action that
requests `auth_self` can fail to authenticate even with the correct password, so the
policy deliberately does not use it. Make sure you are in the `sudo` group (`id -nG`).

**Fans do not change at all in full speed**
The CPU fan responds on the M7400QE (roughly 2500 → 6100 RPM). The GPU fan has been
observed to stay near its idle speed even with `pwm2_enable=0`, because the firmware
keeps regulating it. The mode is still written and reported correctly.

**You want no password prompt at all**
You can make the sysfs nodes group-writable with a udev rule instead, but that lets any
local process spin your fans. If you go that route, keep the helper as the only writer
and give the rule a dedicated group rather than `a+w`.

## Limitations

- Full speed is a mode switch, not a speed curve: the driver exposes no per-fan PWM value.
- RPM reads can briefly report `0` immediately after a mode change while the fan ramps.
- The tray icon needs a StatusNotifier host; on desktops without one the window works
  normally and the status bar says the tray is unavailable.

## License

MIT — see [LICENSE](LICENSE).
