; Iris Color Windows x64 installer. Build through tools/build_windows_installer.py.
; All destination paths are fixed at runtime. Payload paths are validated and
; enumerated at build time. Unknown/user files are never recursively removed.

Target amd64-unicode
Unicode true
RequestExecutionLevel admin
ManifestDPIAware true
SetCompressor /SOLID lzma
CRCCheck force
SetDatablockOptimize on
Name "Iris Color"
Caption "Iris Color ${IRIS_VERSION} Setup"
OutFile "${IRIS_OUTPUT}"
InstallDir "$PROGRAMFILES64\Iris Color"
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "${IRIS_VERSION_QUAD}"
VIAddVersionKey "ProductName" "Iris Color"
VIAddVersionKey "ProductVersion" "${IRIS_VERSION}"
VIAddVersionKey "FileVersion" "${IRIS_VERSION}"
VIAddVersionKey "FileDescription" "Iris Color Windows x64 VST3 Installer"
VIAddVersionKey "LegalCopyright" "Iris Color contributors"

!include MUI2.nsh
!include LogicLib.nsh
!include x64.nsh
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\IrisColorVST3"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Install Iris Color ${IRIS_VERSION}"
!define MUI_WELCOMEPAGE_TEXT "Iris Color adds musical color to your sound.$\r$\n$\r$\nSave your project and close FL Studio or any other host using Iris Color before installing.$\r$\n$\r$\nThe Windows x64 VST3 is installed in the standard system VST3 folder. An existing copy at that location is updated. Your presets and other plugins are preserved."
!define MUI_FINISHPAGE_TITLE "Iris Color is installed"
!define MUI_FINISHPAGE_TEXT "Open your DAW and rescan its plugins.$\r$\n$\r$\nIn FL Studio, open Options > Manage plugins, enable Verify plugins and Rescan previously verified plugins, then choose Find installed plugins.$\r$\n$\r$\nLoad Iris Color in a Mixer effect slot."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var BundleDir
Var MetaDir
Var CheckPath
Var CopySource
Var SetupMutex

!include "${IRIS_PAYLOAD_NSH}"

!macro DefineInit PREFIX
Function ${PREFIX}.onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Iris Color requires 64-bit Windows and a host that loads x64 VST3 plugins." /SD IDOK
    SetErrorLevel 1633
    Quit
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  StrCpy $BundleDir "$COMMONFILES64\VST3\Iris Color.vst3"
  StrCpy $MetaDir "$PROGRAMFILES64\Iris Color"
  ; Explicitly ignore NSIS /D overrides. We do not offer arbitrary install roots.
  StrCpy $INSTDIR "$MetaDir"
  System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Global\IrisColorSetup-v1") p .r0 ?e'
  Pop $1
  StrCpy $SetupMutex $0
  ${If} $0 == 0
  ${OrIf} $1 == 183
    MessageBox MB_OK|MB_ICONSTOP "Another Iris Color installer or uninstaller is running. Close it and try again." /SD IDOK
    SetErrorLevel 1618
    Quit
  ${EndIf}
FunctionEnd
!macroend

!insertmacro DefineInit ""
!insertmacro DefineInit "un"

; These preflights never write file contents. They reject existing reparse
; points (including developer symlinks) and stop before touching a loaded DLL.
!macro DefineChecks PREFIX
Function ${PREFIX}CheckDirectory
  System::Call 'kernel32::GetFileAttributesW(w "$CheckPath") i .r0'
  ${If} $0 == -1
    Return
  ${EndIf}
  IntOp $1 $0 & 0x400
  IntOp $2 $0 & 0x10
  ${If} $1 != 0
  ${OrIf} $2 == 0
    MessageBox MB_OK|MB_ICONSTOP "This location is a link or a file, not a normal plugin directory:$\r$\n$\r$\n$CheckPath$\r$\n$\r$\nMove the existing item yourself, then run Setup again." /SD IDOK
    SetErrorLevel 87
    Abort
  ${EndIf}
FunctionEnd

Function ${PREFIX}CheckFile
  System::Call 'kernel32::GetFileAttributesW(w "$CheckPath") i .r0'
  ${If} $0 == -1
    Return
  ${EndIf}
  IntOp $1 $0 & 0x410
  ${If} $1 != 0
    MessageBox MB_OK|MB_ICONSTOP "The expected plugin file is a link or directory:$\r$\n$\r$\n$CheckPath$\r$\n$\r$\nMove this item yourself, then try again." /SD IDOK
    SetErrorLevel 87
    Abort
  ${EndIf}
check_again:
  ; GENERIC_READ|GENERIC_WRITE, no sharing, OPEN_EXISTING. Never CREATE_ALWAYS.
  System::Call 'kernel32::CreateFileW(w "$CheckPath", i 0xC0000000, i 0, p 0, i 3, i 0x80, p 0) p .r0'
  ${If} $0 == -1
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Iris Color could not access this file:$\r$\n$\r$\n$CheckPath$\r$\n$\r$\nSave your project and close FL Studio or any other host using Iris Color, then choose Retry. If it still fails, check file permissions.$\r$\n$\r$\nCancel leaves Setup unfinished; it never forces a host to close." /SD IDCANCEL IDRETRY check_again
    SetErrorLevel 32
    Abort
  ${EndIf}
  System::Call 'kernel32::CloseHandle(p r0)'
FunctionEnd
!macroend

!insertmacro DefineChecks ""
!insertmacro DefineChecks "un."

Function EnsureDirectory
retry:
  ClearErrors
  CreateDirectory "$CheckPath"
  ${If} ${Errors}
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Unable to create the installation directory:$\r$\n$\r$\n$CheckPath$\r$\n$\r$\nCheck available disk space and permissions, then retry." /SD IDCANCEL IDRETRY retry
    SetErrorLevel 5
    Abort
  ${EndIf}
FunctionEnd

Function InstallFile
retry:
  ClearErrors
  CopyFiles /SILENT "$CopySource" "$CheckPath"
  ${If} ${Errors}
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Unable to install:$\r$\n$\r$\n$CheckPath$\r$\n$\r$\nSave your work, close hosts using Iris Color, and check available disk space. Retry to continue or cancel and run this installer again." /SD IDCANCEL IDRETRY retry
    SetErrorLevel 5
    Abort
  ${EndIf}
FunctionEnd

Function un.RetryDelete
retry:
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Unable to remove:$\r$\n$\r$\n$CheckPath$\r$\n$\r$\nSave your work and close hosts using Iris Color, then retry." /SD IDCANCEL IDRETRY attempt
  SetErrorLevel 5
  Abort
attempt:
  ClearErrors
  Delete "$CheckPath"
  IfErrors retry
FunctionEnd

Section "Iris Color VST3"
  InitPluginsDir
  !insertmacro IrisStagePayload
  SetOutPath "$PLUGINSDIR"
  File /oname=payload-manifest.json "${IRIS_PAYLOAD_MANIFEST}"

  ; Preflight every destination before installing any payload files.
  StrCpy $CheckPath "$COMMONFILES64\VST3"
  Call CheckDirectory
  StrCpy $CheckPath "$MetaDir"
  Call CheckDirectory
  StrCpy $CheckPath "$MetaDir\Uninstall.exe"
  Call CheckFile
  StrCpy $CheckPath "$MetaDir\payload-manifest.json"
  Call CheckFile
  !insertmacro IrisPreflight ""
  !insertmacro IrisInstallPayload

  StrCpy $CheckPath "$MetaDir"
  Call EnsureDirectory
  StrCpy $CopySource "$PLUGINSDIR\payload-manifest.json"
  StrCpy $CheckPath "$MetaDir\payload-manifest.json"
  Call InstallFile
  ClearErrors
  WriteUninstaller "$MetaDir\Uninstall.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "Iris Color"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${IRIS_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Iris Color contributors"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$BundleDir"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$MetaDir\Uninstall.exe,0"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '$\"$MetaDir\Uninstall.exe$\"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$MetaDir\Uninstall.exe$\" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" ${IRIS_INSTALL_KB}
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONSTOP "The plugin files were copied, but Setup could not register the uninstaller. Run this installer again to finish the installation." /SD IDOK
    SetErrorLevel 5
    Abort
  ${EndIf}
  SetErrorLevel 0
SectionEnd

Section "Uninstall"
  StrCpy $CheckPath "$COMMONFILES64\VST3"
  Call un.CheckDirectory
  StrCpy $CheckPath "$MetaDir"
  Call un.CheckDirectory
  StrCpy $CheckPath "$MetaDir\payload-manifest.json"
  Call un.CheckFile
  !insertmacro IrisPreflight "un."
  !insertmacro IrisRemovePayload

  ; NSIS normally executes a temporary copy of the uninstaller, allowing the
  ; installed copy to be removed. Unknown metadata files are also preserved.
  ClearErrors
  Delete "$MetaDir\payload-manifest.json"
  ${If} ${Errors}
    StrCpy $CheckPath "$MetaDir\payload-manifest.json"
    Call un.RetryDelete
  ${EndIf}
  Delete "$MetaDir\Uninstall.exe"
  ${If} ${Errors}
    StrCpy $CheckPath "$MetaDir\Uninstall.exe"
    Call un.RetryDelete
  ${EndIf}
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  RMDir "$MetaDir"
  ClearErrors
  SetErrorLevel 0
SectionEnd
