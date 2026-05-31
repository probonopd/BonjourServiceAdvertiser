# Bonjour Service Advertiser

Advertises Windows network services over mDNS/DNS-SD (Bonjour) so they are automatically discoverable by macOS Finder, avahi, and other Bonjour clients on the local network — with no configuration required.

## Requirements

- Windows 10 or 11 (64-bit)
- Apple Bonjour for Windows — installed automatically with [iTunes](https://www.apple.com/itunes/) or the Apple Devices app, or [downloaded separately](https://support.apple.com/kb/DL999)

## Installation

1. Download `BonjourServiceAdvertiserSetup.exe` from [Releases](../../releases/latest)
2. Run the installer (Administrator required)
3. Click **Install** — the service starts automatically at the end

During setup you can optionally enable:

| Option | Effect |
|---|---|
| **Install OpenSSH Server** | Installs the Windows optional feature and starts `sshd` |
| **Allow empty passwords** | Sets `PermitEmptyPasswords yes` in `sshd_config` and restarts `sshd` |

## What gets advertised

| Service | mDNS type | Condition |
|---|---|---|
| SSH | `_ssh._tcp` | `sshd` is running on port 22 |
| SFTP | `_sftp-ssh._tcp` | `sshd` is running on port 22 |
| File sharing | `_smb._tcp` | LanmanServer is running on port 445 |
| Remote Desktop | `_rdp._tcp` | TermService is running on port 3389 |
| HTTP | `_http._tcp` | Port 80 is listening |
| HTTPS | `_https._tcp` | Port 443 is listening |
| FTP | `_ftp._tcp` | FTP service is running |
| WebDAV | `_webdav._tcp` | WebDAV is enabled |
| Printers | `_ipp._tcp` | Shared printers are present |
| Device info | `_device-info._tcp` | Always — shows hardware model and OS version |

## Configuration

The config file is created on first run at:

```
C:\ProgramData\BonjourServiceAdvertiser\advertiser.ini
```

Edit it to disable built-in services or add custom DNS-SD entries. Changes take effect automatically without restarting.

### Custom advertisement example

```ini
[custom:jellyfin]
enabled=true
name=Jellyfin
type=_http._tcp
port=8096
txt.path=/web/
```

## Uninstall

Open **Settings → Apps**, find **Bonjour Service Advertiser**, and click **Uninstall**.

The configuration file in `%ProgramData%\BonjourServiceAdvertiser\` is preserved.

## Building from source

Requires MSYS2 with the MinGW-w64 toolchain.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows
cmake --build build
```

Build the installer (requires [NSIS 3.x](https://nsis.sourceforge.io/)):

```bash
makensis -DBUILDDIR=build -DSRCDIR=. installer/installer.nsi
```
