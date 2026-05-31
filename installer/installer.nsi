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
!define MUI_FINISHPAGE_RUN         "$INSTDIR\BonjourServiceAdvertiser.exe"
!define MUI_FINISHPAGE_RUN_TEXT    "Launch with --run-console (for testing)"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--run-console"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

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
