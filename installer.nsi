; Bloom Installer Script for NSIS
; Requires: NSIS (Nullsoft Scriptable Install System)

; Include helper for size calculation
!include "FileFunc.nsh"

!define APPNAME "Bloom"
!define COMPANYNAME "Bloom"
!define DESCRIPTION "Media Client for Jellyfin and Silo Servers"
!define VERSIONMAJOR 0
!define VERSIONMINOR 8
!define VERSIONBUILD 0

!define HELPURL "https://github.com/crowquillx/Bloom"
!define UPDATEURL "https://github.com/crowquillx/Bloom/releases"
!define ABOUTURL "https://github.com/crowquillx/Bloom"

; Installation directory
InstallDir "$PROGRAMFILES64\${APPNAME}"

; Request application privileges
RequestExecutionLevel admin

; Modern UI
!include "MUI2.nsh"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

; Installer information
Name "${APPNAME}"
OutFile "Bloom-Setup-${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}.exe"
; Icon "icon.ico"  ; Uncomment when you add an icon.ico file
; UninstallIcon "icon.ico"  ; Uncomment when you add an icon.ico file

; Version info
VIProductVersion "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "CompanyName" "${COMPANYNAME}"
VIAddVersionKey "LegalCopyright" "© ${COMPANYNAME}"
VIAddVersionKey "FileDescription" "${DESCRIPTION}"
VIAddVersionKey "FileVersion" "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}"

; A running Bloom.exe cannot be opened for writing. Silent installs default to
; ignoring files they cannot overwrite, which would leave a mixed install, so
; wait for the updater-launched Bloom to exit (up to 60 s) before copying.
Function WaitForBloomExit
    IfFileExists "$INSTDIR\Bloom.exe" 0 done
    StrCpy $R0 0
    retry:
        ClearErrors
        FileOpen $R1 "$INSTDIR\Bloom.exe" a
        IfErrors 0 unlocked
        IfSilent 0 prompt
        IntOp $R0 $R0 + 1
        IntCmp $R0 120 give_up 0 give_up
        Sleep 500
        Goto retry
    prompt:
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Bloom is running. Close it, then select Retry." IDRETRY retry
    give_up:
        SetErrorLevel 2
        Abort "Bloom is still running."
    unlocked:
        FileClose $R1
    done:
FunctionEnd

; Default section
Section "Install"
    ; Bloom is 64-bit and reads the 64-bit registry view.
    SetRegView 64
    Call WaitForBloomExit

    ; Set output path to installation directory
    SetOutPath $INSTDIR

    ; Copy all files from package directory
    File /r "Bloom-Windows\*.*"
    
    ; Create Start Menu shortcuts
    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\Bloom.exe"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    
    ; Create Desktop shortcut (optional)
    CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\Bloom.exe"
    
    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    
    ; Registry keys for Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayIcon" "$INSTDIR\Bloom.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Publisher" "${COMPANYNAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "HelpLink" "${HELPURL}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "URLUpdateInfo" "${UPDATEURL}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "URLInfoAbout" "${ABOUTURL}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "VersionMajor" ${VERSIONMAJOR}
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "VersionMinor" ${VERSIONMINOR}
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoRepair" 1
    
    ; Estimate install size (in KB)
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "EstimatedSize" "$0"

    ; Older installers registered under the 32-bit (WOW6432Node) view.
    SetRegView 32
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
    SetRegView 64

    ; The in-app updater passes /RELAUNCH. Starting Bloom through Explorer runs
    ; it unelevated as the signed-in user instead of inheriting admin rights.
    ${GetParameters} $R0
    ClearErrors
    ${GetOptions} $R0 "/RELAUNCH" $R1
    IfErrors +2 0
    Exec '"$WINDIR\explorer.exe" "$INSTDIR\Bloom.exe"'
SectionEnd

; Uninstaller section
Section "Uninstall"
    ; Remove files
    RMDir /r "$INSTDIR"
    
    ; Remove shortcuts
    RMDir /r "$SMPROGRAMS\${APPNAME}"
    Delete "$DESKTOP\${APPNAME}.lnk"
    
    ; Remove registry keys from both views; older installers wrote the 32-bit one.
    SetRegView 64
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
    DeleteRegKey HKLM "Software\${APPNAME}"
    SetRegView 32
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
    DeleteRegKey HKLM "Software\${APPNAME}"
SectionEnd
