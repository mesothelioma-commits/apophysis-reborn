#Requires -Version 5.1
<#
.SYNOPSIS
Exercise the per-user installer in new test folders on a Windows x64 machine.
.DESCRIPTION
Requires a separate test account with no registered Apophysis Reborn installations.
Use -PreviousInstaller to exercise an upgrade from a real earlier Windows release.
Keeps all evidence and deliberately retained sample files. Does
not launch the editor, change user preferences, or recursively clean directories.
Close other Apophysis Reborn processes before running. This script must itself be
executed on Windows; a Linux compile or Wine startup is not a lifecycle PASS.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-installer-windows.ps1 -Installer C:\release\apophysis-reborn-0.10.0-windows-x64-setup.exe -Payload C:\release\apophysis-reborn-0.10.0-windows-x64 -OutDirectory C:\temp\apo-installer-check
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Installer,
    [Parameter(Mandatory=$true)][string]$Payload,
    [Parameter(Mandatory=$true)][string]$OutDirectory,
    [string]$PreviousInstaller,
    [switch]$NoOpen
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function OrdinaryPath([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -Force -LiteralPath $current
            Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Reparse path refused: $current"
        }
        $parent = [IO.Directory]::GetParent($current)
        $current = if ($parent) { $parent.FullName } else { $null }
    }
}
function ChildPath([string]$Root, [string]$Name) {
    Require (-not [string]::IsNullOrEmpty($Name)) 'Empty manifest name'
    foreach ($part in $Name.Split('/')) {
        Require ($part -and $part -notin @('.', '..') -and $part -notmatch '[<>:"\\|?*\x00-\x1f]' -and
                 $part -notmatch '[. ]$' -and $part -notmatch '^(CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9\u00b9\u00b2\u00b3]|LPT[1-9\u00b9\u00b2\u00b3])(\.|$)') "Unsafe manifest name: $Name"
    }
    return [IO.Path]::Combine($Root, $Name.Replace('/', '\'))
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function WriteNew([string]$Path, [string]$Text) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
}
function SnapshotRegistry([string[]]$Roots) {
    $records = @()
    foreach ($root in $Roots) {
        if (-not (Test-Path -LiteralPath $root)) { $records += "MISSING:$root"; continue }
        $keys = @((Get-Item -LiteralPath $root)) + @(Get-ChildItem -LiteralPath $root -Recurse)
        foreach ($key in $keys | Sort-Object Name) {
            $values = [ordered]@{}
            foreach ($name in $key.GetValueNames() | Sort-Object) {
                $values[$name] = @($key.GetValueKind($name).ToString(), $key.GetValue($name, $null, 'DoNotExpandEnvironmentNames'))
            }
            $records += (@($key.Name, $values) | ConvertTo-Json -Compress -Depth 20)
        }
    }
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($records -join "`n"))
        return ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally { $algorithm.Dispose() }
}

Require ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) 'Run this verifier on Windows.'
Require ([Environment]::Is64BitProcess) 'Use 64-bit PowerShell on Windows x64.'
$Installer = [IO.Path]::GetFullPath($Installer)
$Payload = [IO.Path]::GetFullPath($Payload)
$OutDirectory = [IO.Path]::GetFullPath($OutDirectory)
Require ($OutDirectory -match '^[A-Za-z]:\\' -and $OutDirectory.Length -lt 90) 'Use a short absolute local-drive evidence path (under 90 characters).'
foreach ($path in @($Installer, $Payload, $OutDirectory)) { OrdinaryPath $path }
Require (Test-Path -LiteralPath $Installer -PathType Leaf) 'Installer file is missing.'
Require (-not (Test-Path -LiteralPath $OutDirectory)) 'Evidence directory already exists; choose a new folder.'
[void][IO.Directory]::CreateDirectory($OutDirectory)
$reportPath = Join-Path $OutDirectory 'installer-lifecycle.json'
$transcriptPath = Join-Path $OutDirectory 'installer-lifecycle.txt'
$report = [ordered]@{
    schema = 'apophysis-reborn.installer-lifecycle'; schema_version = 1
    result = 'FAIL'; windows_executed = $true; signed_package_required = $false
    os_version = [Environment]::OSVersion.VersionString
    started_utc = [DateTime]::UtcNow.ToString('o'); steps = @(); error = $null
}
$savedTemp = $env:TEMP
$savedTmp = $env:TMP
$junction = $null
$conflictShortcut = $null
$conflictHash = $null
$heldFile = $null
$unrelatedKey = $null
$unrelatedId = $null
$recoveryProbe = $null
$recoveryHash = $null
$probeFolders = @()
Start-Transcript -LiteralPath $transcriptPath | Out-Null
function Passed([string]$Name) {
    $script:report.steps += [ordered]@{ name = $Name; result = 'PASS' }
    Write-Host "PASS: $Name"
}
function RunInstaller([string]$Destination, [string]$Options = '', [string]$Setup = $Installer) {
    # NSIS requires /D= to be the final argument, without quotes even with spaces.
    $arguments = "/S $Options /D=$Destination"
    Write-Host "Executing installer: $arguments"
    $process = Start-Process -FilePath $Setup -ArgumentList $arguments -Wait -PassThru
    return $process.ExitCode
}
function RunUninstaller([string]$Directory) {
    Write-Host "Executing uninstaller in $Directory"
    # -Wait waits for the NSIS temporary child as well as the original process.
    $process = Start-Process -FilePath (Join-Path $Directory 'Uninstall.exe') -ArgumentList '/S' -Wait -PassThru
    return $process.ExitCode
}
function VerifyInstalled([string]$Directory) {
    foreach ($property in $script:manifest.files.PSObject.Properties) {
        $path = ChildPath $Directory $property.Name
        OrdinaryPath $path
        Require ((Hash $path) -eq $property.Value.sha256) "Installed file differs: $($property.Name)"
        Require ((Get-Item -LiteralPath $path).Length -eq $property.Value.bytes) "Installed size differs: $($property.Name)"
    }
    foreach ($name in @('package-manifest.json', 'SHA256SUMS.txt')) {
        Require ((Hash (Join-Path $Directory $name)) -eq (Hash (Join-Path $Payload $name))) "Installed metadata differs: $name"
    }
    $actual = @(Get-ChildItem -LiteralPath $Directory -File -Recurse)
    Require ($actual.Count -eq @($script:manifest.files.PSObject.Properties).Count + 3) 'Installed file inventory has unexpected entries.'
    $registration = Get-ItemProperty -LiteralPath $script:registrationPath
    Require ($registration.InstallLocation -eq $Directory -and $registration.DisplayVersion -eq $script:version) 'Registration identity mismatch.'
    Require ($registration.PayloadSHA256 -eq $script:report.payload_manifest_sha256) 'Registered manifest identity differs.'
}

try {
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $Payload 'package-manifest.json') | ConvertFrom-Json
    Require ($manifest.schema -eq 'apophysis-reborn.runtime-package' -and $manifest.schema_version -eq 1) 'Unsupported payload manifest.'
    $version = [string]$manifest.version
    Require ($version -match '^\d+\.\d+\.\d+$' -and $manifest.source_commit -match '^[0-9a-f]{40}$') 'Invalid release identity.'
    $report.version = $version
    $report.source_commit = $manifest.source_commit
    $report.installer_sha256 = Hash $Installer
    $report.payload_manifest_sha256 = Hash (Join-Path $Payload 'package-manifest.json')
    $registrationPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ApophysisReborn-$version"
    $menuShortcut = Join-Path ([Environment]::GetFolderPath('Programs')) "Apophysis Reborn\Apophysis Reborn $version.lnk"
    $desktopShortcut = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) "Apophysis Reborn $version.lnk"
    $uninstallRoot = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
    if (Test-Path -LiteralPath $uninstallRoot) {
        $installed = @(Get-ChildItem -LiteralPath $uninstallRoot | Where-Object { $_.PSChildName -like 'ApophysisReborn-*' })
        Require ($installed.Count -eq 0) 'An Apophysis Reborn installation is already registered. Use an empty Windows test account so the upgrade verifier cannot uninstall personal installations.'
    }
    Require (-not (Test-Path -LiteralPath $menuShortcut) -and -not (Test-Path -LiteralPath $desktopShortcut)) 'Versioned shortcuts already exist; the verifier will not overwrite them.'
    foreach ($path in @($menuShortcut, $desktopShortcut)) { OrdinaryPath $path }
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($property in $manifest.files.PSObject.Properties) {
        Require ($seen.Add($property.Name)) 'Case-insensitive manifest collision.'
        $path = ChildPath $Payload $property.Name
        OrdinaryPath $path
        Require ((Hash $path) -eq $property.Value.sha256 -and (Get-Item -LiteralPath $path).Length -eq $property.Value.bytes) "Payload hash or size differs: $($property.Name)"
    }
    $sumNames = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in Get-Content -LiteralPath (Join-Path $Payload 'SHA256SUMS.txt')) {
        Require ($line -match '^([0-9a-f]{64})  (.+)$') 'Malformed SHA256SUMS line.'
        $expectedHash = $Matches[1]; $name = $Matches[2]
        Require ($sumNames.Add($name)) 'Duplicate checksum name.'
        Require ((Hash (ChildPath $Payload $name)) -eq $expectedHash) "Checksum failed: $name"
    }
    Require ($sumNames.Count -eq $seen.Count + 1 -and $sumNames.Contains('package-manifest.json')) 'Checksum coverage differs.'
    Require (@(Get-ChildItem -LiteralPath $Payload -File -Recurse).Count -eq $seen.Count + 2) 'Unlisted payload files.'
    foreach ($name in $seen) { Require ($sumNames.Contains($name)) "Checksum missing: $name" }
    Passed 'Input inventory, checksum coverage and new-test-account preconditions'

    $watchedKeys = @('HKCU:\Software\ApophysisReborn', 'HKCU:\Software\Classes\.flame', 'HKCU:\Software\Classes\.apoproject',
                    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.flame',
                    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.apoproject')
    $registryBefore = SnapshotRegistry $watchedKeys
    $report.watched_registry_hash_before = $registryBefore
    $privateTemp = Join-Path $OutDirectory 'logs'
    [void][IO.Directory]::CreateDirectory($privateTemp)
    $env:TEMP = $privateTemp; $env:TMP = $privateTemp
    $outside = Join-Path $OutDirectory 'user files'
    [void][IO.Directory]::CreateDirectory($outside)
    $project = Join-Path $outside 'personal.apoproject'
    WriteNew $project 'Personal project sentinel: must survive every operation.'
    $projectHash = Hash $project

    if ($PreviousInstaller) {
        $PreviousInstaller = [IO.Path]::GetFullPath($PreviousInstaller)
        OrdinaryPath $PreviousInstaller
        Require (Test-Path -LiteralPath $PreviousInstaller -PathType Leaf) 'Previous installer is missing.'
        $previousInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($PreviousInstaller)
        $oldVersion = [string]$previousInfo.ProductVersion
        Require ($previousInfo.ProductName -eq 'Apophysis Reborn' -and $oldVersion -match '^[0-9]+\.[0-9]+\.[0-9]+$' -and
                 ([version]$oldVersion -lt [version]$version)) 'Supply the Apophysis Reborn setup executable for an earlier release.'
        $report.previous_version = $oldVersion
        $report.previous_installer_sha256 = Hash $PreviousInstaller
        $oldRoot = Join-Path $OutDirectory ('previous custom ' + [char]0x00e9)
        $oldKey = Join-Path $uninstallRoot "ApophysisReborn-$oldVersion"
        $oldMenu = Join-Path ([Environment]::GetFolderPath('Programs')) "Apophysis Reborn\Apophysis Reborn $oldVersion.lnk"
        $oldDesktop = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) "Apophysis Reborn $oldVersion.lnk"
        foreach ($path in @($oldMenu, $oldDesktop)) {
            OrdinaryPath $path
            Require (-not (Test-Path -LiteralPath $path)) "Previous version shortcut already exists: $path"
        }
        # Qt's AppLocalDataLocation for the editor contains its recovery snapshots.
        $data = [Environment]::GetFolderPath('LocalApplicationData')
        foreach ($segment in @('ApophysisReborn', 'Apophysis Reborn', 'recovery')) {
            $data = Join-Path $data $segment
            OrdinaryPath $data
            if (-not (Test-Path -LiteralPath $data)) {
                [void][IO.Directory]::CreateDirectory($data)
                $probeFolders = @($data) + $probeFolders
            }
        }
        $recoveryProbe = Join-Path $data ('verifier-' + [Guid]::NewGuid().ToString('N') + '.apoproject')
        WriteNew $recoveryProbe 'Recovery data in the per-user application directory.'
        $recoveryHash = Hash $recoveryProbe
        Require ((RunInstaller $oldRoot '/DESKTOP' $PreviousInstaller) -eq 0) 'Previous installer failed.'
        $oldRegistration = Get-ItemProperty -LiteralPath $oldKey
        Require ($oldRegistration.InstallLocation -eq $oldRoot -and $oldRegistration.DisplayVersion -eq $oldVersion) 'Previous registration mismatch.'
        $oldEditor = Join-Path $oldRoot 'apophysis-reborn.exe'
        $oldEditorHash = Hash $oldEditor
        $oldReadme = Join-Path $oldRoot 'START-HERE.txt'
        [IO.File]::AppendAllText($oldReadme, "`r`nEdited after old installation.")
        $oldReadmeHash = Hash $oldReadme
        $oldArtwork = Join-Path $oldRoot 'saved artwork.apoproject'
        WriteNew $oldArtwork 'Personal project in the previous custom program folder.'
        $oldArtworkHash = Hash $oldArtwork
        $oldKeySnapshot = SnapshotRegistry @($oldKey)
        foreach ($path in @($oldMenu, $oldDesktop)) {
            Require (Test-Path -LiteralPath $path -PathType Leaf) "Previous installer did not create $path"
        }
        # A portable sibling and an unrelated registry entry also contain the product name.
        $portable = Join-Path $OutDirectory 'portable'
        [void][IO.Directory]::CreateDirectory($portable)
        $portableFile = Join-Path $portable 'apophysis-reborn.exe'
        Copy-Item -LiteralPath $oldEditor -Destination $portableFile
        $unrelatedId = [Guid]::NewGuid().ToString('N')
        $unrelatedKey = Join-Path $uninstallRoot "ApophysisReborn-Tools-$unrelatedId"
        [void](New-Item -Path $unrelatedKey)
        [void](New-ItemProperty -LiteralPath $unrelatedKey -Name 'VerifierId' -Value $unrelatedId)
        [void](New-ItemProperty -LiteralPath $unrelatedKey -Name 'DisplayName' -Value 'Apophysis Reborn test tools')
        [void](New-ItemProperty -LiteralPath $unrelatedKey -Name 'InstallLocation' -Value $portable)
        $unrelatedSnapshot = SnapshotRegistry @($unrelatedKey)
        Passed 'Previous release installed at a custom path with versioned shortcuts and retained user files'
    }

    $nonempty = Join-Path $OutDirectory 'nonempty'
    [void][IO.Directory]::CreateDirectory($nonempty)
    $existing = Join-Path $nonempty 'keep.txt'
    WriteNew $existing 'Existing destination content.'
    $existingHash = Hash $existing
    Require ((RunInstaller $nonempty) -ne 0) 'Nonempty destination was accepted.'
    Require ((Hash $existing) -eq $existingHash -and @(Get-ChildItem -LiteralPath $nonempty).Count -eq 1) 'Nonempty destination changed.'
    Require (-not (Test-Path -LiteralPath $registrationPath)) 'Failed install left a registration.'
    Passed 'Nonempty destinations are rejected without replacing files'

    $junction = Join-Path $OutDirectory 'junction-parent'
    New-Item -ItemType Junction -Path $junction -Target $outside | Out-Null
    Require ((RunInstaller (Join-Path $junction 'must-not-appear')) -ne 0) 'Junction parent was accepted.'
    Require (-not (Test-Path -LiteralPath (Join-Path $outside 'must-not-appear')) -and (Hash $project) -eq $projectHash) 'Installation followed the junction.'
    [IO.Directory]::Delete($junction); $junction = $null
    Passed 'Junction ancestors are rejected without touching their targets'

    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($menuShortcut))
    WriteNew $menuShortcut 'Pre-existing shortcut sentinel.'
    $conflictShortcut = $menuShortcut; $conflictHash = Hash $menuShortcut
    $conflict = Join-Path $OutDirectory 'shortcut-conflict'
    Require ((RunInstaller $conflict) -ne 0) 'Existing shortcut was overwritten.'
    Require ((Hash $menuShortcut) -eq $conflictHash) 'Existing shortcut bytes changed.'
    Require (-not (Test-Path -LiteralPath $registrationPath)) 'Shortcut conflict left a registration.'
    Require (-not (Test-Path -LiteralPath $conflict) -or @(Get-ChildItem -LiteralPath $conflict -File -Recurse).Count -eq 0) 'Ordinary install failure did not roll back copied files.'
    [IO.File]::Delete($menuShortcut); $conflictShortcut = $null
    Passed 'Shortcut conflicts preserve the existing file and roll back copied payload'

    if ($PreviousInstaller) {
        Require ((SnapshotRegistry @($oldKey)) -eq $oldKeySnapshot -and (Hash $oldEditor) -eq $oldEditorHash -and
                 (Test-Path -LiteralPath $oldMenu) -and (Test-Path -LiteralPath $oldDesktop)) 'A failed new installation changed the previous installation.'
        Passed 'Old installation survives both destination rejection and new-install rollback'

        # All registered plugin files will appear missing through the junction.
        # Its target still has an empty directory with the name of an owned subfolder.
        $oldPlugins = Join-Path $oldRoot 'plugins'
        OrdinaryPath $oldPlugins
        $oldManifest = Get-Content -Raw -LiteralPath (Join-Path $oldRoot 'package-manifest.json') | ConvertFrom-Json
        $nestedPlugin = $oldManifest.files.PSObject.Properties.Name |
            Where-Object { $_ -like 'plugins/*/*' } | Select-Object -First 1
        Require ([bool]$nestedPlugin) 'Previous payload has no nested plugin directory for the junction check.'
        $pluginSubfolder = $nestedPlugin.Split('/')[1]
        $externalPlugins = Join-Path $outside 'empty plugin tree'
        $emptySubfolder = ChildPath $externalPlugins $pluginSubfolder
        [void][IO.Directory]::CreateDirectory($emptySubfolder)
        [IO.Directory]::Move($oldPlugins, (Join-Path $OutDirectory 'saved previous plugins'))
        New-Item -ItemType Junction -Path $oldPlugins -Target $externalPlugins | Out-Null
        $junction = $oldPlugins
    }

    $install = Join-Path $OutDirectory ('installed with spaces ' + [char]0x00e9)
    Require ((RunInstaller $install) -eq 0) 'Clean silent installation failed.'
    VerifyInstalled $install
    Require (Test-Path -LiteralPath $menuShortcut -PathType Leaf) 'Default Start Menu shortcut missing.'
    Require (-not (Test-Path -LiteralPath $desktopShortcut)) 'Desktop shortcut was created without opt-in.'
    Passed 'Clean per-user installation supports spaces and Unicode and verifies every payload byte'

    if ($PreviousInstaller) {
        Require (-not (Test-Path -LiteralPath $oldKey) -and -not (Test-Path -LiteralPath $oldEditor) -and
                 -not (Test-Path -LiteralPath (Join-Path $oldRoot 'flame-cpu.exe')) -and
                 -not (Test-Path -LiteralPath (Join-Path $oldRoot 'Uninstall.exe'))) 'Previous program or uninstall registration remains after the upgrade.'
        Require (-not (Test-Path -LiteralPath $oldMenu) -and -not (Test-Path -LiteralPath $oldDesktop)) 'An owned previous-version shortcut remains.'
        Require ((Hash $oldReadme) -eq $oldReadmeHash -and (Hash $oldArtwork) -eq $oldArtworkHash -and
                 (Hash $project) -eq $projectHash -and (Hash $recoveryProbe) -eq $recoveryHash) 'Upgrade changed a retained user file.'
        Require ((Hash $portableFile) -eq $oldEditorHash -and (SnapshotRegistry @($unrelatedKey)) -eq $unrelatedSnapshot) 'Upgrade touched a portable folder or unrelated registration.'
        Passed 'Upgrade removes the previous owned program and shortcuts and preserves edited and personal files'
        Require ((Test-Path -LiteralPath $oldPlugins -PathType Container) -and
                 ((Get-Item -LiteralPath $oldPlugins).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -and
                 (Test-Path -LiteralPath $emptySubfolder -PathType Container) -and
                 (Test-Path -LiteralPath (Join-Path $oldPlugins $pluginSubfolder) -PathType Container) -and
                 @(Get-ChildItem -Force -LiteralPath $emptySubfolder).Count -eq 0) 'Upgrade removed or changed a junction or its external empty subfolder.'
        [IO.Directory]::Delete($oldPlugins); $junction = $null
        Passed 'Upgrade preserves a junction and its external empty subfolder when all registered files below it are missing'
    }

    $second = Join-Path $OutDirectory 'same-version-second-location'
    Require ((RunInstaller $second) -ne 0) 'A second registration of the same version was accepted.'
    VerifyInstalled $install
    Require (-not (Test-Path -LiteralPath $second)) 'Rejected same-version installation created a second folder.'
    Passed 'Same-version reinstall is refused and the existing installation is unchanged'

    $heldFile = [IO.File]::Open((Join-Path $install 'flame-cpu.exe'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    [void](RunUninstaller $install)
    Require (Test-Path -LiteralPath $registrationPath) 'Locked-file uninstall removed the registration.'
    VerifyInstalled $install
    $heldFile.Dispose(); $heldFile = $null
    Passed 'Locked-file uninstall fails before deleting the first payload file'

    $example = @($manifest.files.PSObject.Properties.Name | Where-Object { $_ -like 'examples/*.flame' } | Sort-Object)[0]
    Require ($example) 'Payload has no example for preservation tests.'
    $examples = Join-Path $install 'examples'
    $savedExamples = Join-Path $install 'saved-examples'
    [IO.Directory]::Move($examples, $savedExamples)
    $externalExample = ChildPath $outside $example.Substring('examples/'.Length)
    Copy-Item -LiteralPath (ChildPath $savedExamples $example.Substring('examples/'.Length)) -Destination $externalExample
    $externalHash = Hash $externalExample
    New-Item -ItemType Junction -Path $examples -Target $outside | Out-Null
    $junction = $examples
    [void](RunUninstaller $install)
    Require (Test-Path -LiteralPath $registrationPath) 'Inner-junction uninstall removed the registration.'
    Require ((Hash $externalExample) -eq $externalHash -and (Hash $project) -eq $projectHash) 'Uninstall followed an inner junction.'
    [IO.Directory]::Delete($junction); $junction = $null
    [IO.Directory]::Move($savedExamples, $examples)
    VerifyInstalled $install
    Passed 'A replaced payload directory cannot redirect uninstall into external files'

    $modifiedExample = ChildPath $install $example
    [IO.File]::AppendAllText($modifiedExample, "`r`nUser-edited example sentinel.")
    $modifiedHash = Hash $modifiedExample
    $extra = Join-Path $install 'my-project.apoproject'
    WriteNew $extra 'User project saved alongside the application.'
    $extraHash = Hash $extra
    $readOnly = Join-Path $install 'START-HERE.txt'
    [IO.File]::SetAttributes($readOnly, [IO.FileAttributes]::ReadOnly)
    $readOnlyHash = Hash $readOnly
    $hardlink = Join-Path $outside 'hardlinked-qt.conf'
    New-Item -ItemType HardLink -Path $hardlink -Target (Join-Path $install 'qt.conf') | Out-Null
    $hardlinkHash = Hash $hardlink
    # An editable manifest cannot authorize deletion; the uninstaller uses the
    # inventory and hashes compiled into its private helper instead.
    $changedManifest = Join-Path $install 'package-manifest.json'
    [IO.File]::AppendAllText($changedManifest, "`r`nThis user edit must be preserved.")
    $changedManifestHash = Hash $changedManifest
    [void](RunUninstaller $install)
    Require (-not (Test-Path -LiteralPath $registrationPath)) 'Uninstall registration remains.'
    Require (-not (Test-Path -LiteralPath $menuShortcut) -and -not (Test-Path -LiteralPath $desktopShortcut)) 'Owned unchanged shortcut remains.'
    $retained = @($example, 'START-HERE.txt', 'qt.conf', 'package-manifest.json')
    foreach ($name in @($manifest.files.PSObject.Properties.Name) + @('SHA256SUMS.txt', 'Uninstall.exe')) {
        if ($name -notin $retained) { Require (-not (Test-Path -LiteralPath (ChildPath $install $name))) "Unchanged installed file remains: $name" }
    }
    Require ((Hash $modifiedExample) -eq $modifiedHash -and (Hash $extra) -eq $extraHash) 'Modified or extra files were removed or changed.'
    Require ((Hash $readOnly) -eq $readOnlyHash -and (Hash $hardlink) -eq $hardlinkHash -and (Hash (Join-Path $install 'qt.conf')) -eq $hardlinkHash) 'Read-only or hard-linked files were removed or changed.'
    Require ((Hash $changedManifest) -eq $changedManifestHash -and (Hash $project) -eq $projectHash) 'Editable manifest or external project was changed.'
    Passed 'Uninstall removes only unchanged owned files and preserves modified, extra, read-only and hard-linked files'

    $clean = Join-Path $OutDirectory 'clean-reinstall'
    Require ((RunInstaller $clean '/NOSTARTMENU /DESKTOP') -eq 0) 'Reinstall into a new empty folder failed.'
    VerifyInstalled $clean
    Require (-not (Test-Path -LiteralPath $menuShortcut) -and (Test-Path -LiteralPath $desktopShortcut -PathType Leaf)) 'Explicit shortcut choices were not respected.'
    [void](RunUninstaller $clean)
    Require (-not (Test-Path -LiteralPath $registrationPath) -and -not (Test-Path -LiteralPath $clean) -and -not (Test-Path -LiteralPath $desktopShortcut)) 'Clean uninstall left program files, registration or shortcut.'
    Passed 'Reinstall after uninstall, desktop opt-in, Start Menu opt-out and clean uninstall'

    if ($PreviousInstaller) {
        $lockedOld = Join-Path $OutDirectory 'locked previous'
        $fresh = Join-Path $OutDirectory 'upgrade with locked predecessor'
        Require ((RunInstaller $lockedOld '' $PreviousInstaller) -eq 0) 'Previous install for the locked-file test failed.'
        $heldFile = [IO.File]::Open((Join-Path $lockedOld 'flame-cpu.exe'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        Require ((RunInstaller $fresh) -eq 20) 'Expected installed-with-previous-cleanup-pending status for a locked previous release.'
        VerifyInstalled $fresh
        Require ((Test-Path -LiteralPath $oldKey) -and (Hash (Join-Path $lockedOld 'apophysis-reborn.exe')) -eq $oldEditorHash) 'Locked predecessor lost its registry entry or executable.'
        $heldFile.Dispose(); $heldFile = $null
        [void](RunUninstaller $lockedOld)
        Require (-not (Test-Path -LiteralPath $oldKey) -and -not (Test-Path -LiteralPath $lockedOld)) 'Previous uninstall retry did not complete.'
        VerifyInstalled $fresh
        [void](RunUninstaller $fresh)
        Require (-not (Test-Path -LiteralPath $registrationPath) -and -not (Test-Path -LiteralPath $fresh)) 'Clean removal of the upgraded release failed.'
        Require ((Hash $recoveryProbe) -eq $recoveryHash) 'Pending cleanup or retry changed per-user recovery data.'
        Passed 'A locked predecessor can be uninstalled later while the new installation remains usable'
    }

    $registryAfter = SnapshotRegistry $watchedKeys
    $report.watched_registry_hash_after = $registryAfter
    Require ($registryAfter -eq $registryBefore) 'User preferences or file associations changed.'
    Require ((Hash $project) -eq $projectHash -and (Hash $existing) -eq $existingHash -and (Hash $externalExample) -eq $externalHash) 'External user data changed.'
    Passed 'User settings, known file associations and external data are preserved'
    $report.result = 'PASS'
} catch {
    $report.error = $_.Exception.Message
    Write-Host "FAIL: $($report.error)"
} finally {
    if ($heldFile) { $heldFile.Dispose() }
    if ($recoveryHash -and (Test-Path -LiteralPath $recoveryProbe)) {
        try {
            OrdinaryPath $recoveryProbe
            if ((Hash $recoveryProbe) -eq $recoveryHash) { [IO.File]::Delete($recoveryProbe) }
        } catch { Write-Warning "Recovery probe retained for inspection: $recoveryProbe" }
    }
    foreach ($folder in $probeFolders) {
        try {
            OrdinaryPath $folder
            if (Test-Path -LiteralPath $folder) { [IO.Directory]::Delete($folder) }
        } catch { Write-Warning "Recovery folder retained for inspection: $folder" }
    }
    if ($junction -and (Test-Path -LiteralPath $junction)) {
        # Delete only the junction itself. Never recurse through its target.
        try { [IO.Directory]::Delete($junction) } catch { Write-Warning "Junction retained for inspection: $junction" }
    }
    if ($conflictShortcut -and (Test-Path -LiteralPath $conflictShortcut)) {
        try { if ((Hash $conflictShortcut) -eq $conflictHash) { [IO.File]::Delete($conflictShortcut) } } catch { }
    }
    if ($unrelatedKey -and (Test-Path -LiteralPath $unrelatedKey)) {
        $key = Get-Item -LiteralPath $unrelatedKey
        if ($key.GetValue('VerifierId') -eq $unrelatedId -and $key.SubKeyCount -eq 0) {
            Remove-Item -LiteralPath $unrelatedKey
        }
    }
    $env:TEMP = $savedTemp; $env:TMP = $savedTmp
    $report.finished_utc = [DateTime]::UtcNow.ToString('o')
    $report | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $reportPath -Encoding UTF8
    Write-Host "Result: $($report.result). Evidence: $reportPath"
    Write-Host 'All test folders and retained sample files remain available for inspection.'
    Stop-Transcript | Out-Null
    if (-not $NoOpen) { Start-Process -FilePath explorer.exe -ArgumentList ('/select,"' + $reportPath + '"') }
}
if ($report.result -eq 'PASS') { exit 0 } else { exit 1 }
