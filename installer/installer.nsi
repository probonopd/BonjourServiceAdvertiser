; Bonjour Service Advertiser — NSIS installer script
; NSIS is GPL-licensed: https://nsis.sourceforge.io/License
;
; Build:
;   makensis /DBUILDDIR=<path\to\build> /DSRCDIR=<repo_root> installer.nsi
;
; Requires NSIS 3.x

Unicode True
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"

; ============================================================================
; Options dialog variables
; ============================================================================
Var Dialog
Var Checkbox_SSH
Var Checkbox_EmptyPwd
Var SSH_State
Var EmptyPwd_State

; ============================================================================
; Defines (can be overridden from command line with /D)
; ============================================================================
!ifndef BUILDDIR
  !define BUILDDIR "..\build"
!endif
!ifndef SRCDIR
  !define SRCDIR ".."
!endif
!ifndef VERSION
  !define VERSION "1.0.0"
!endif

; ============================================================================
; Metadata
; ============================================================================
Name              "Bonjour Service Advertiser"
OutFile           "${BUILDDIR}\BonjourServiceAdvertiserSetup.exe"
InstallDir        "$PROGRAMFILES64\BonjourServiceAdvertiser"
InstallDirRegKey  HKLM "Software\BonjourServiceAdvertiser" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails   show
ShowUnInstDetails show

; ============================================================================
; Version info
; ============================================================================
VIProductVersion  "${VERSION}.0"
VIAddVersionKey /LANG=0 "ProductName"     "Bonjour Service Advertiser"
VIAddVersionKey /LANG=0 "ProductVersion"  "${VERSION}"
VIAddVersionKey /LANG=0 "FileVersion"     "${VERSION}.0"
VIAddVersionKey /LANG=0 "FileDescription" "Bonjour Service Advertiser Setup"
VIAddVersionKey /LANG=0 "LegalCopyright"  "See LICENSE"

; ============================================================================
; MUI pages
; ============================================================================
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
Page custom OptionsPage OptionsPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ============================================================================
; Options page
; ============================================================================
Function OptionsPage
  !insertmacro MUI_HEADER_TEXT "Additional Options" "Choose optional features to install."
  nsDialogs::Create 1018
  Pop $Dialog
  ${If} $Dialog == error
    Abort
  ${EndIf}

  ${NSD_CreateCheckbox} 0 10u 100% 12u "Install OpenSSH Server (enables SSH access to this computer)"
  Pop $Checkbox_SSH
  ${NSD_SetState} $Checkbox_SSH $SSH_State

  ${NSD_CreateCheckbox} 0 30u 100% 12u "Allow empty passwords for SSH login"
  Pop $Checkbox_EmptyPwd
  ${NSD_SetState} $Checkbox_EmptyPwd $EmptyPwd_State

  nsDialogs::Show
FunctionEnd

Function OptionsPageLeave
  ${NSD_GetState} $Checkbox_SSH $SSH_State
  ${NSD_GetState} $Checkbox_EmptyPwd $EmptyPwd_State
FunctionEnd

; ============================================================================
; Install section
; ============================================================================
Section "Main" SecMain
  SetOutPath "$INSTDIR"

  ; Stop and remove existing service (ignore errors)
  ExecWait 'sc stop BonjourServiceAdvertiser'  $0
  Sleep 1500
  ExecWait '"$INSTDIR\BonjourServiceAdvertiser.exe" --uninstall' $0

  ; Install executable
  File "${BUILDDIR}\BonjourServiceAdvertiser.exe"

  ; Install default config only if not already present
  SetOutPath "$COMMONPROGRAMDATA\BonjourServiceAdvertiser"
  ${IfNot} ${FileExists} "$COMMONPROGRAMDATA\BonjourServiceAdvertiser\advertiser.ini"
    File /oname=advertiser.ini "${SRCDIR}\installer\advertiser.ini.default"
  ${EndIf}

  ; Register and start the Windows service
  SetOutPath "$INSTDIR"
  ExecWait '"$INSTDIR\BonjourServiceAdvertiser.exe" --install' $0
  ExecWait 'sc start BonjourServiceAdvertiser' $0

  ; Install OpenSSH if requested
  ${If} $SSH_State == ${BST_CHECKED}
    DetailPrint "Installing OpenSSH Server..."
    nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -Command "Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0"'
    nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Service -Name sshd -StartupType Automatic; Start-Service sshd -ErrorAction SilentlyContinue"'
  ${EndIf}

  ; Configure SSH to allow empty passwords if requested
  ${If} $EmptyPwd_State == ${BST_CHECKED}
    DetailPrint "Configuring SSH to allow empty passwords..."
    FileOpen $R0 "$TEMP\bsa_sshcfg.ps1" w
    FileWrite $R0 '$$f = "$COMMONPROGRAMDATA\ssh\sshd_config"$\r$\n'
    FileWrite $R0 'if (-not (Test-Path $$f)) { exit 0 }$\r$\n'
    FileWrite $R0 '$$c = Get-Content $$f -Raw$\r$\n'
    FileWrite $R0 'if ($$c -match "(?im)^#?[ \t]*PermitEmptyPasswords[ \t]+\S+") {$\r$\n'
    FileWrite $R0 '    $$c = $$c -replace "(?im)^#?[ \t]*PermitEmptyPasswords[ \t]+\S+", "PermitEmptyPasswords yes"$\r$\n'
    FileWrite $R0 '} else {$\r$\n'
    FileWrite $R0 '    $$c = $$c.TrimEnd() + "`r`nPermitEmptyPasswords yes`r`n"$\r$\n'
    FileWrite $R0 '}$\r$\n'
    FileWrite $R0 'Set-Content -Path $$f -Value $$c -NoNewline$\r$\n'
    FileWrite $R0 'Restart-Service sshd -ErrorAction SilentlyContinue$\r$\n'
    FileClose $R0
    nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP\bsa_sshcfg.ps1"'
    Delete "$TEMP\bsa_sshcfg.ps1"
  ${EndIf}

  ; Write Add/Remove Programs entry
  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BonjourServiceAdvertiser" \
    "DisplayName" "Bonjour Service Advertiser"
  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BonjourServiceAdvertiser" \
    "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BonjourServiceAdvertiser" \
    "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BonjourServiceAdvertiser" \
    "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BonjourServiceAdvertiser" \
    "Publisher" "BonjourServiceAdvertiser"
  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BonjourServiceAdvertiser" \
    "DisplayVersion" "${VERSION}"
  WriteRegDWORD HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BonjourServiceAdvertiser" \
    "NoModify" 1
  WriteRegDWORD HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BonjourServiceAdvertiser" \
    "NoRepair" 1

  WriteRegStr HKLM "Software\BonjourServiceAdvertiser" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; ============================================================================
; Uninstall section
; ============================================================================
Section "Uninstall"
  ; Stop and remove service
  ExecWait 'sc stop BonjourServiceAdvertiser' $0
  Sleep 1500
  ExecWait '"$INSTDIR\BonjourServiceAdvertiser.exe" --uninstall' $0

  ; Remove files
  Delete "$INSTDIR\BonjourServiceAdvertiser.exe"
  Delete "$INSTDIR\uninstall.exe"
  RMDir  "$INSTDIR"

  ; Remove registry entries
  DeleteRegKey HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BonjourServiceAdvertiser"
  DeleteRegKey HKLM "Software\BonjourServiceAdvertiser"

  ; Leave config/log data intact (user data)
SectionEnd
