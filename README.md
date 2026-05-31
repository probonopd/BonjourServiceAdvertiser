# Bonjour Auto Advertiser for Windows

## Purpose

Provide automatic Bonjour (mDNS/DNS-SD) advertisement of commonly available Windows-hosted services.

The service continuously detects selected Windows services and publishes corresponding DNS-SD advertisements through Apple's Bonjour SDK.

The product shall install and operate with minimal user interaction.

---

# Product Goals

## Automatic Discovery

Detect and advertise:

* SSH
* SMB
* RDP
* HTTP
* HTTPS
* FTP
* WebDAV
* Printer services

## Manual Advertisements

Allow administrators to define additional DNS-SD advertisements through a configuration file.

No plugin system shall be implemented.

---

# Deployment Requirements

## Installation

The product shall be distributed as a single installer executable.

Installation shall require no configuration.

User workflow:

1. Download installer
2. Run installer
3. Click Install

The service starts automatically.

No command-line options required.

---

## Uninstallation

The installer shall register a standard Windows uninstall entry.

User workflow:

1. Open Apps & Features
2. Select product
3. Click Uninstall

Uninstallation shall:

* Stop the service
* Remove the service
* Remove program files
* Remove Start Menu entries
* Remove uninstall registration

Configuration files may optionally be preserved if the user chooses.

---

## Installer Technology

Preferred:

* WiX Toolset

Alternative:

* Inno Setup

Installer output:

BonjourServiceAdvertiserSetup.exe

Single-file installer.

---

# Service Details

Service Name:

BonjourServiceAdvertiser

Display Name:

Bonjour Service Advertiser

Startup Type:

Automatic (Delayed Start)

Account:

LocalService

---

# Technology Stack

Language:

C++20

Compiler:

MinGW-w64 GCC

Build System:

CMake

Dependencies:

* Apple Bonjour SDK
* Win32 API
* IP Helper API

No Cygwin runtime dependency permitted.

---

# High-Level Architecture

+----------------------+
| Windows Service      |
+-----------+----------+
|
v
+----------------------+
| Detector Manager     |
+-----------+----------+
|
v
+----------------------+
| Service Registry     |
+-----------+----------+
|
v
+----------------------+
| Bonjour Publisher    |
+----------------------+

---

# Built-In Detectors

## SSH

Requirements:

* sshd service running
* Port 22 listening

Advertisement:

_ssh._tcp

---

## SMB

Requirements:

* LanmanServer running
* Port 445 listening

Advertisement:

_smb._tcp

---

## RDP

Requirements:

* TermService running
* Port 3389 listening

Advertisement:

_rdp._tcp

---

## HTTP

Requirements:

* HTTP listener detected

Advertisement:

_http._tcp

---

## HTTPS

Requirements:

* HTTPS listener detected

Advertisement:

_https._tcp

---

## FTP

Requirements:

* FTP service running

Advertisement:

_ftp._tcp

---

## WebDAV

Requirements:

* WebDAV enabled

Advertisement:

_webdav._tcp

---

## Printers

Requirements:

* Shared printer available

Advertisement:

_ipp._tcp

---

# Configuration

Configuration file:

advertiser.ini

Location:

%ProgramData%\BonjourServiceAdvertiser\advertiser.ini

The service monitors the file and reloads changes automatically.

---

# Built-In Service Controls

Example:

[services]
ssh=true
smb=true
rdp=true
http=true
https=true
ftp=false
webdav=false
printers=true

---

# Custom Advertisements

Administrators may define arbitrary DNS-SD advertisements.

Example:

[custom:jenkins]
enabled=true
name=Jenkins
type=_http._tcp
port=8080

[custom:minecraft]
enabled=true
name=Minecraft Server
type=_minecraft._tcp
port=25565

[custom:git]
enabled=true
name=Git HTTP
type=_http._tcp
port=3000

---

# TXT Records

TXT records may be specified.

Example:

[custom:jenkins]
enabled=true
name=Jenkins
type=_http._tcp
port=8080
txt.path=/
txt.version=2.504

Produces:

path=/
version=2.504

---

# Validation

Custom advertisements shall be validated before publication.

Validation rules:

* Port 1-65535
* Valid DNS-SD service type
* Valid service name
* TXT keys ≤255 bytes
* TXT values ≤255 bytes

Invalid entries shall be ignored and logged.

---

# Advertisement Rules

Automatic advertisements require:

Service running
AND
Port listening
AND
At least one non-loopback interface

Custom advertisements require:

Enabled
AND
Port listening

Custom advertisements shall not require a Windows service.

---

# Bonjour Integration

Bonjour shall be loaded dynamically.

Implementation:

LoadLibrary("dnssd.dll")

If Bonjour is unavailable:

* Service remains running
* Warning logged
* Retry every 60 seconds

When Bonjour becomes available:

* Publish all advertisements automatically

---

# Event Handling

Use:

NotifyServiceStatusChange()

for service lifecycle events.

Use:

NotifyIpInterfaceChange()

for network changes.

Perform full reconciliation every:

300 seconds

---

# Logging

Destinations:

* Windows Event Log
* Rolling log files

Directory:

%ProgramData%\BonjourServiceAdvertiser\logs

Format:

JSON Lines

Example:

{
"time":"2026-05-31T12:30:00Z",
"level":"INFO",
"event":"advertised",
"service":"_ssh._tcp",
"port":22
}

Maximum file size:

50 MB

Retention:

10 files

---

# Security Policy

Automatically advertise only:

* SSH
* SMB
* RDP
* HTTP
* HTTPS
* FTP
* WebDAV
* Printers

Never automatically advertise:

* WinRM
* WMI
* RPC
* DCOM
* SQL Server
* LDAP
* Kerberos

Custom advertisements may advertise any port explicitly configured by the administrator.

---

# Acceptance Criteria

Installer:

* One-click installation
* One-click uninstall

Service:

* Starts automatically after installation
* Survives reboot
* Recovers from Bonjour restarts

Discovery:

* Advertisements visible from macOS Finder
* Advertisements visible via dns-sd
* Advertisements removed within 30 seconds of service shutdown

Configuration:

* INI changes applied without restarting the service

Reliability:

* No crash when Bonjour is missing
* No crash when network interfaces change
* No crash when monitored services stop unexpectedly

One thing I'd add before implementation begins: bundle a Bonjour runtime check into the installer. If Bonjour is not already installed, the installer should either (a) install a bundled Bonjour redistributable silently or (b) clearly state that Bonjour is required and offer to install it. Requiring users to discover and install Bonjour manually would make the "one-click" goal fail in practice.
