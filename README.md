# Prism Monitor

Fast, native system monitor for Linux — live CPU, memory, GPU, storage,
network, process, sensor and power telemetry with custom Cairo charts on
GTK 4.

![Version](https://img.shields.io/badge/version-1.3.0-blue)
![GTK](https://img.shields.io/badge/GTK-4-green)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

## Features

18 pages in a custom-drawn GTK 4 shell (sidebar, header bar, status bar):

- **Monitor:** Overview, CPU, Memory, GPU, Storage, Network
- **System:** Processes, Sensors, System, Power & Frequency
- **Manage:** Services, Startup, Connections, Installed Apps, Drivers, Disk Space
- **Tools:** Benchmarks, Flight Recorder

Plus:

- 16 themes + accents, interface font/scale, compact density, graph
  fill/grid/glow, smoothing, FPS/quality controls
- Process search, sort, pause/resume, end/force-kill (with confirmation)
- Benchmarks (CPU single/multi, GPU renderer, disk read/write, internet)
  with persistent history (`benchmarks.tsv`)
- Flight recorder — timestamped TSV telemetry in the user data directory
- systemd services, autostart entries, sockets, kernel modules,
  Flatpak/Snap/desktop apps, disk-space analyser

### New in 1.3.0

- Process I/O + tree view, renice/affinity/kill-tree
- Persistent history (`history.tsv`, restored on launch, 5 MB rotation)
- Chart PNG export + recorder replay (CPU/Mem/Net charts from TSV)
- SMART/NVMe health, AMD/Intel GPU fan/power/VRAM, Wi-Fi signal + ping
- CPU governor/EPP/turbo controls, journal viewer, connection kill (`ss -K`)
- DiskSpace trash/delete, duplicate-size finder, treemap bars
- Tray mode (close minimizes), autostart, `--tray/--minimized/--daemon`
- Custom dashboard: show/hide/reorder pages (Settings → Dashboard)
- Plugins (`~/.config/prism-monitor/plugins/`, `name=value unit`)
- Alert hooks (env `PRISM_ALERT_MSG`) + `alerts.log`
- Diagnostics bundle (`--diagnostics`, palette), Prometheus exporter (`--exporter PORT`)
- Flatpak manifest, gettext skeleton (`po/`, `make i18n`), CI with ASan

### New in 1.2.0

- **CLI:** `--help`, `--version`, `--page`, `--interval`, `--paused`,
  headless `--snapshot` (JSON) and `--record` (TSV) — see below
- **Pause / Resume sampling** (header button, `Ctrl+P`) — freeze charts
- **Threshold alerts** — temperature / disk / memory / battery toasts
  with Settings → Alerts thresholds and 60 s cooldown
- **Command palette** (`Ctrl+K`) — jump to any page or run an action
- **Export** — snapshot JSON (`Ctrl+E`) and recorder CSV/JSON, with
  peak/avg stats per recording
- **Session restore** — window size and last page are remembered

## Build

```bash
sudo apt install build-essential pkg-config libgtk-4-dev
make -j$(nproc)
make test
./bin/prism-monitor
```

Build a Debian package:

```bash
make deb
# -> dist/prism-monitor_1.2.0_amd64.deb
sudo dpkg -i dist/prism-monitor_1.2.0_amd64.deb
```

Install from source:

```bash
make install            # to /usr/local by default (PREFIX=/usr/local)
sudo make install PREFIX=/usr
```

## Command line

```text
prism-monitor --help
prism-monitor --version
prism-monitor --page cpu --interval 0.5 --paused
prism-monitor --snapshot /tmp/snap.json --duration 2
prism-monitor --snapshot --duration 1          # JSON to stdout
prism-monitor --record /tmp/rec.tsv --duration 10 --interval 0.5
```

## Shortcuts

| Keys | Action |
|---|---|
| `Ctrl+K` | Command palette |
| `Ctrl+P` | Pause / resume sampling |
| `Ctrl+E` | Export snapshot JSON |
| `Ctrl+F` | Focus process search |
| `Ctrl+B` | Collapse / expand sidebar |
| `Ctrl+,` | Settings |
| `Ctrl+1…9`, `Ctrl+Tab` | Jump to page |
| `F5` | Show sampling status |
| `Ctrl+Q` | Quit |

## Layout

```text
src/main.c          GTK entry + CLI
src/core/           config, sampler thread, history, parse, format, alerts, export
src/sys/            /proc, /sys, hwmon, nvidia-smi, systemctl readers
src/ui/             shell, theme, widgets, settings, 18 pages
tests/test_core.c   unit tests (parse/format/history/alerts/export)
data/               .desktop + AppStream metadata
assets/             icon
```

Preferences live in `~/.config/prism-monitor/config.ini`;
recordings and benchmark history in `~/.local/share/prism-monitor/`.

## License

MIT — see [LICENSE](LICENSE).
