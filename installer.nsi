; Bloom Installer Script for NSIS
; Requires: NSIS (Nullsoft Scriptable Install System)
;
; Build after scripts/package-windows.ps1 has staged Bloom-Windows\. CI passes
; the version explicitly: makensis /DVERSION=X.Y.Z installer.nsi. Without it,
; the version comes from the VERSION file.

; Include helper for size calculation
!include "FileFunc.nsh"

!define APPNAME "Bloom"
!define COMPANYNAME "Bloom"
!define DESCRIPTION "Media Client for Jellyfin and Silo Servers"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"

!ifndef VERSION
    !define /file VERSION "VERSION"
!endif
!searchparse /noerrors "${VERSION}" "" VERSIONMAJOR "." VERSIONMINOR "." VERSIONBUILD
!ifndef VERSIONBUILD
    !error "VERSION must be X.Y.Z (got '${VERSION}')"
!endif

!define HELPURL "https://github.com/crowquillx/Bloom"
!define UPDATEURL "https://github.com/crowquillx/Bloom/releases"
!define ABOUTURL "https://github.com/crowquillx/Bloom"

; Installation directory. An existing install's registered location wins over
; the default; /D= on the command line wins over both. InstallDirRegKey reads
; only the 32-bit view, which pre-0.8 installers used; .onInit reads the 64-bit
; key that current installers write.
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "${UNINSTALL_KEY}" "InstallLocation"

; Request application privileges
RequestExecutionLevel admin

; Modern UI
!define MUI_ICON "src\resources\images\app\logo.ico"
!define MUI_UNICON "src\resources\images\app\logo.ico"
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
OutFile "Bloom-Setup-${VERSION}.exe"

; Version info
VIProductVersion "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "CompanyName" "${COMPANYNAME}"
VIAddVersionKey "LegalCopyright" "© ${COMPANYNAME}"
VIAddVersionKey "FileDescription" "${DESCRIPTION}"
VIAddVersionKey "FileVersion" "${VERSION}"

; package-windows.ps1 stages bloom-install-manifest.txt: every packaged file,
; then every packaged directory deepest-first, relative to $INSTDIR. Upgrades
; and the uninstaller delete exactly those files and remove directories only
; once they are empty, so files that belong to anything else stay.
!define MANIFEST "bloom-install-manifest.txt"

; Fallback for installs made before the manifest existed. It removes whole Qt
; directories and every DLL, so it runs only in the default
; $PROGRAMFILES64\Bloom, which an older installer created and only
; administrators can write to.
!macro RemoveBloomPayload
    Delete "$INSTDIR\Bloom.exe"
    Delete "$INSTDIR\bloom-seek-preview.exe"
    Delete "$INSTDIR\vc_redist.x64.exe"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\qt.conf"
    Delete "$INSTDIR\mpv-provenance.json"
    RMDir /r "$INSTDIR\qml"
    RMDir /r "$INSTDIR\plugins"
    RMDir /r "$INSTDIR\share"
    RMDir /r "$INSTDIR\translations"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\platforminputcontexts"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\iconengines"
    RMDir /r "$INSTDIR\generic"
    RMDir /r "$INSTDIR\multimedia"
    RMDir /r "$INSTDIR\networkinformation"
    RMDir /r "$INSTDIR\tls"
    RMDir /r "$INSTDIR\sqldrivers"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\qmltooling"
    RMDir /r "$INSTDIR\position"
    RMDir /r "$INSTDIR\texttospeech"
    RMDir /r "$INSTDIR\virtualkeyboard"
    RMDir /r "$INSTDIR\scenegraph"
!macroend

; Deletes what the installed manifest lists. Sets $R9 to 1 when a manifest was
; found, 0 otherwise. Absolute entries, and entries containing "..", ":" or
; the Delete wildcards "*" and "?", are skipped.
!macro DEFINE_REMOVE_MANIFEST_PAYLOAD UN
Function ${UN}RemoveManifestPayload
    StrCpy $R9 0
    IfFileExists "$INSTDIR\${MANIFEST}" 0 done
    ClearErrors
    FileOpen $R0 "$INSTDIR\${MANIFEST}" r
    IfErrors done
    next_line:
        ClearErrors
        FileRead $R0 $R1
        IfErrors end_of_file
    strip_newline:
        StrCpy $R2 $R1 1 -1
        StrCmp $R2 "$\r" +2
        StrCmp $R2 "$\n" 0 stripped
        StrCpy $R1 $R1 -1
        Goto strip_newline
    stripped:
        StrCmp $R1 "" next_line
        StrCpy $R2 $R1 1
        StrCmp $R2 "\" next_line
        StrCmp $R2 "/" next_line
        StrLen $R3 $R1
        StrCpy $R4 0
    scan:
        IntCmp $R4 $R3 safe 0 safe
        StrCpy $R2 $R1 2 $R4
        StrCmp $R2 ".." next_line
        StrCpy $R2 $R1 1 $R4
        StrCmp $R2 ":" next_line
        StrCmp $R2 "*" next_line
        StrCmp $R2 "?" next_line
        IntOp $R4 $R4 + 1
        Goto scan
    safe:
        IfFileExists "$INSTDIR\$R1\*.*" 0 remove_file
        RMDir "$INSTDIR\$R1"
        Goto next_line
    remove_file:
        Delete "$INSTDIR\$R1"
        Goto next_line
    end_of_file:
    FileClose $R0
    Delete "$INSTDIR\${MANIFEST}"
    StrCpy $R9 1
    done:
FunctionEnd
!macroend
!insertmacro DEFINE_REMOVE_MANIFEST_PAYLOAD ""
!insertmacro DEFINE_REMOVE_MANIFEST_PAYLOAD "un."

; Removes the previous payload: the manifest when there is one, otherwise the
; legacy list, but only in the default install folder. A pre-manifest install
; anywhere else may share its folder with other files, so it is overwritten in
; place, as older installers did; the manifest written now covers the next
; upgrade.
!macro RemovePreviousPayload UN
    Call ${UN}RemoveManifestPayload
    StrCmp $R9 1 previous_payload_removed
    StrCmp $INSTDIR "$PROGRAMFILES64\${APPNAME}" 0 previous_payload_removed
    !insertmacro RemoveBloomPayload
    previous_payload_removed:
!macroend

; /D= replaces $INSTDIR before .onInit, so only the default is replaced here.
Function .onInit
    StrCmp $INSTDIR "$PROGRAMFILES64\${APPNAME}" 0 done
    SetRegView 64
    ReadRegStr $R0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
    SetRegView 32
    StrCmp $R0 "" done
    StrCpy $INSTDIR $R0
    done:
FunctionEnd

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

; Install into a folder Bloom owns. A new install into a typed folder such as
; C:\Apps goes to C:\Apps\Bloom; an existing Bloom install upgrades in place.
Function EnsureDedicatedInstallDir
    IfFileExists "$INSTDIR\Bloom.exe" done
    ${GetFileName} "$INSTDIR" $R0
    StrCmp $R0 "${APPNAME}" done
    StrCpy $INSTDIR "$INSTDIR\${APPNAME}"
    done:
FunctionEnd

; Default section
Section "Install"
    ; Shortcuts go to the all-users Start menu and public desktop rather than
    ; the profile of the administrator who approved elevation.
    SetShellVarContext all
    ; Bloom is 64-bit and reads the 64-bit registry view.
    SetRegView 64
    Call EnsureDedicatedInstallDir
    Call WaitForBloomExit

    ; Remove the previous version's payload so files dropped by this version
    ; do not linger.
    !insertmacro RemovePreviousPayload ""

    ; Set output path to installation directory
    SetOutPath $INSTDIR

    ; Copy all files from package directory
    File /r "Bloom-Windows\*.*"

    ; Older installers created shortcuts in the elevating user's profile.
    SetShellVarContext current
    RMDir /r "$SMPROGRAMS\${APPNAME}"
    Delete "$DESKTOP\${APPNAME}.lnk"
    SetShellVarContext all

    ; Create Start Menu shortcuts
    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\Bloom.exe"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"

    ; Create Desktop shortcut (optional)
    CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\Bloom.exe"

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Registry keys for Add/Remove Programs
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APPNAME}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\Bloom.exe"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${COMPANYNAME}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "HelpLink" "${HELPURL}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "URLUpdateInfo" "${UPDATEURL}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "${ABOUTURL}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "VersionMajor" ${VERSIONMAJOR}
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "VersionMinor" ${VERSIONMINOR}
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1

    ; Estimate install size (in KB)
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" "$0"

    ; Older installers registered under the 32-bit (WOW6432Node) view.
    SetRegView 32
    DeleteRegKey HKLM "${UNINSTALL_KEY}"
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
    SetShellVarContext all

    ; Remove only what Bloom installed; the directory itself goes only once it
    ; is empty, so a folder chosen by the user keeps its other contents.
    !insertmacro RemovePreviousPayload "un."
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    ; Remove shortcuts, including ones older installers put in the elevating
    ; user's profile.
    RMDir /r "$SMPROGRAMS\${APPNAME}"
    Delete "$DESKTOP\${APPNAME}.lnk"
    SetShellVarContext current
    RMDir /r "$SMPROGRAMS\${APPNAME}"
    Delete "$DESKTOP\${APPNAME}.lnk"

    ; Remove registry keys from both views; older installers wrote the 32-bit one.
    SetRegView 64
    DeleteRegKey HKLM "${UNINSTALL_KEY}"
    DeleteRegKey HKLM "Software\${APPNAME}"
    SetRegView 32
    DeleteRegKey HKLM "${UNINSTALL_KEY}"
    DeleteRegKey HKLM "Software\${APPNAME}"
SectionEnd
