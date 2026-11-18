; The private guard owns copying, hash-checked removal and per-user registration.
; NSIS supplies the Unicode wizard and trusted, CRC-protected payload extraction.
Unicode true
RequestExecutionLevel user
ManifestDPIAware true
CRCCheck force
SetCompressor zlib
SetDatablockOptimize on
SetOverwrite off
ShowInstDetails show
ShowUninstDetails show
Name "Apophysis Reborn ${APO_VERSION}"
OutFile "${APO_OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\Apophysis Reborn\${APO_VERSION}"
VIProductVersion "${APO_VERSION}.0"
VIAddVersionKey "ProductName" "Apophysis Reborn"
VIAddVersionKey "FileDescription" "Apophysis Reborn per-user Windows x64 setup"
VIAddVersionKey "FileVersion" "${APO_VERSION}"
VIAddVersionKey "ProductVersion" "${APO_VERSION}"
VIAddVersionKey "LegalCopyright" "Apophysis Reborn project"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "FileFunc.nsh"

Var GuardResult
Var GuardOutput
Var StartMenuChoice
Var DesktopChoice
Var InstallLog

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Install the editor, command-line renderer, examples and offline documentation for your Windows account.$\r$\n$\r$\nWhen upgrading, setup installs the new release first, then removes the previous installed version. Your projects, preferences and recovery files stay in place.$\r$\n$\r$\nChoose an empty destination folder."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\apophysis-reborn.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_TEXT "Apophysis Reborn is installed.$\r$\n$\r$\nUse Help > User manual to get started. Installation details were saved to:$\r$\n$InstallLog"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Section "Application, examples and documentation (required)" SecApplication
  SectionIn RO
  Call PerformInstall
SectionEnd
Section "Start Menu shortcut for this version" SecStartMenu
SectionEnd
Section /o "Desktop shortcut for this version" SecDesktop
SectionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "This package requires 64-bit Windows."
    SetErrorLevel 2
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "This package requires Windows 10 or newer; Windows 11 is the primary target."
    SetErrorLevel 2
    Abort
  ${EndIf}
  SetShellVarContext current
  SetRegView 64
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/NOSTARTMENU" $1
  ${IfNot} ${Errors}
    SectionSetFlags ${SecStartMenu} 0
  ${EndIf}
  ClearErrors
  ${GetOptions} $0 "/DESKTOP" $1
  ${IfNot} ${Errors}
    SectionSetFlags ${SecDesktop} ${SF_SELECTED}
  ${EndIf}
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=installer-guard.exe "${APO_GUARD}"
FunctionEnd

Function NormalizeDirectory
  StrLen $0 $INSTDIR
  ${If} $0 > 3
    StrCpy $1 $INSTDIR 1 -1
    ${If} $1 == "\"
    ${OrIf} $1 == "/"
      StrCpy $INSTDIR $INSTDIR -1
    ${EndIf}
  ${EndIf}
FunctionEnd

Function DirectoryLeave
  Call NormalizeDirectory
  nsExec::ExecToStack '"$PLUGINSDIR\installer-guard.exe" check "$INSTDIR"'
  Pop $GuardResult
  Pop $GuardOutput
  ${If} $GuardResult != 0
    MessageBox MB_OK|MB_ICONSTOP "$GuardOutput$\r$\n$\r$\nChoose a new or empty folder with ordinary, non-linked parent directories."
    Abort
  ${EndIf}
FunctionEnd

Function PerformInstall
  Call NormalizeDirectory
  SectionGetFlags ${SecStartMenu} $0
  IntOp $StartMenuChoice $0 & ${SF_SELECTED}
  SectionGetFlags ${SecDesktop} $0
  IntOp $DesktopChoice $0 & ${SF_SELECTED}
  GetTempFileName $0 "$TEMP"
  StrCpy $InstallLog "$0.txt"
  ClearErrors
  Rename "$0" "$InstallLog"
  ${If} ${Errors}
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "Cannot reserve an installation log in the temporary folder."
    SetErrorLevel 2
    Abort
  ${EndIf}
  !include "${APO_PAYLOAD_INCLUDE}"
  SetOutPath "$PLUGINSDIR"
  WriteUninstaller "$PLUGINSDIR\Uninstall.exe"
  nsExec::ExecToStack '"$PLUGINSDIR\installer-guard.exe" install "$PLUGINSDIR\payload" "$PLUGINSDIR\Uninstall.exe" "$INSTDIR" $StartMenuChoice $DesktopChoice "$InstallLog"'
  Pop $GuardResult
  Pop $GuardOutput
  DetailPrint "$GuardOutput"
  DetailPrint "Installation log: $InstallLog"
  ${If} $GuardResult == 20
    DetailPrint "New version installed. Previous version cleanup needs attention."
    IfSilent +2
    MessageBox MB_OK|MB_ICONEXCLAMATION "The new version is installed. Setup could not finish removing the previous version.$\r$\n$\r$\nClose the editor and any render processes. Use Windows Installed apps to uninstall the previous version. The installation log identifies the affected version and files.$\r$\n$\r$\nDetails: $InstallLog"
    SetErrorLevel 20
    Return
  ${ElseIf} $GuardResult != 0
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "Installation did not complete.$\r$\n$GuardOutput$\r$\n$\r$\nDetails: $InstallLog"
    SetErrorLevel 2
    Abort
  ${EndIf}
  SetErrorLevel 0
FunctionEnd

Function un.onInit
  SetShellVarContext current
  SetRegView 64
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=installer-guard.exe "${APO_GUARD}"
FunctionEnd

Section "Uninstall"
  GetTempFileName $0 "$TEMP"
  StrCpy $InstallLog "$0.txt"
  ClearErrors
  Rename "$0" "$InstallLog"
  ${If} ${Errors}
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "Cannot reserve an uninstall log in the temporary folder."
    SetErrorLevel 2
    Abort
  ${EndIf}
  nsExec::ExecToStack '"$PLUGINSDIR\installer-guard.exe" uninstall "$INSTDIR" "$InstallLog"'
  Pop $GuardResult
  Pop $GuardOutput
  DetailPrint "$GuardOutput"
  DetailPrint "Uninstall log: $InstallLog"
  ${If} $GuardResult == 10
    IfSilent +2
    MessageBox MB_OK|MB_ICONINFORMATION "This version was uninstalled. Modified or additional files remain in their original locations.$\r$\n$\r$\nYour projects, preferences and recovery files were preserved.$\r$\nDetails: $InstallLog"
  ${ElseIf} $GuardResult != 0
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "Uninstall did not complete. Close the editor and render processes, then retry.$\r$\n$GuardOutput$\r$\n$\r$\nDetails: $InstallLog"
    SetErrorLevel 2
    Abort
  ${EndIf}
  SetErrorLevel 0
SectionEnd
