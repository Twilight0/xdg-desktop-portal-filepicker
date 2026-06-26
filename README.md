# xdg-desktop-portal-xapp-filepicker

A backend implementation for [xdg-desktop-portal](http://github.com/flatpak/xdg-desktop-portal)
that uses GTK and various pieces of Cinnamon/MATE/Xfce4 infrastructure.

This portal backend provides file chooser dialogs (Open, Save, SaveFiles) for
sandboxed applications, flatpaks, and any desktop clients using the portal API.

## Features

- **OpenFile**: Native file/folder picker with filters, multiselect, and initial folder
- **SaveFile**: Single file save with suggested name, initial folder, and overwrite confirmation
- **SaveFiles**: Multi-file save for applications that export multiple files at once
- **Dory Integration**: When Dory is installed, routes file chooser dialogs through Dory's
  native D-Bus interface (`org.Dory.FileChooser`) for a premium file picking experience
- **GTK3 Fallback**: Falls back to native GTK3 file dialogs when Dory is not available
- **Screenshot, Wallpaper, Inhibit, Background, Lockdown**: Additional portal interfaces
  for Cinnamon/MATE/Xfce4 desktop integration

## Installation

### Arch Linux (AUR)

```bash
yay -S xdg-desktop-portal-xapp-filepicker-git
```

### Building from Source

#### Dependencies

- `meson` (>= 0.53.0)
- `libglib2.0-dev` (>= 2.44)
- `libgtk-3-dev` (>= 3.0)
- `xdg-desktop-portal-dev` (>= 1.7.1)
- `systemd-dev` (>= 253) or `systemd` (< 253)

#### Build

```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```

## How It Works

1. Applications call `org.freedesktop.impl.portal.FileChooser` methods
2. This backend checks if `dory` is in `$PATH`
3. If Dory is available, dialogs are routed via `org.Dory.FileChooser` D-Bus
4. If Dory is not available, native GTK3 file dialogs are used as fallback

## D-Bus Interface

The backend implements:
- `org.freedesktop.impl.portal.FileChooser` (OpenFile, SaveFile, SaveFiles)
- `org.freedesktop.impl.portal.Screenshot`
- `org.freedesktop.impl.portal.Inhibit`
- `org.freedesktop.impl.portal.Background`
- `org.freedesktop.impl.portal.Lockdown`
- `org.freedesktop.impl.portal.Wallpaper`

## License

LGPL-2.1-or-later
