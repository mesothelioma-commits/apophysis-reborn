#Requires -Version 5.1
<#
.SYNOPSIS
Exercise the per-user installer in new test folders on a Windows x64 machine.
.DESCRIPTION
Refuses to run when this release is already registered or its versioned shortcuts
already exist. Keeps all evidence and deliberately retained sample files. Does
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
Start-Transcript -LiteralPath $transcriptPath | Out-Null
function Passed([string]$Name) {
    $script:report.steps += [ordered]@{ name = $Name; result = 'PASS' }
    Write-Host "PASS: $Name"
}
function RunInstaller([string]$Destination, [string]$Options = '') {
    # NSIS requires /D= to be the final argument, without quotes even with spaces.
    $arguments = "/S $Options /D=$Destination"
    Write-Host "Executing installer: $arguments"
    $process = Start-Process -FilePath $Installer -ArgumentList $arguments -Wait -PassThru
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
    Require (-not (Test-Path -LiteralPath $registrationPath)) 'This release is already installed. Use a separate Windows test account or uninstall it yourself before running this verifier.'
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
    $otherVersions = @()
    if (Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall') {
        $otherVersions = @(Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' |
                          Where-Object { $_.PSChildName -like 'ApophysisReborn-*' } | ForEach-Object { $_.PSPath })
    }
    $watchedKeys += $otherVersions
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

    $install = Join-Path $OutDirectory ('installed with spaces ' + [char]0x00e9)
    Require ((RunInstaller $install) -eq 0) 'Clean silent installation failed.'
    VerifyInstalled $install
    Require (Test-Path -LiteralPath $menuShortcut -PathType Leaf) 'Default Start Menu shortcut missing.'
    Require (-not (Test-Path -LiteralPath $desktopShortcut)) 'Desktop shortcut was created without opt-in.'
    Passed 'Clean per-user installation supports spaces and Unicode and verifies every payload byte'

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

    $registryAfter = SnapshotRegistry $watchedKeys
    $report.watched_registry_hash_after = $registryAfter
    Require ($registryAfter -eq $registryBefore) 'User preferences, file associations or other version registrations changed.'
    Require ((Hash $project) -eq $projectHash -and (Hash $existing) -eq $existingHash -and (Hash $externalExample) -eq $externalHash) 'External user data changed.'
    Passed 'User settings, known file associations, other version registrations and external data are preserved'
    $report.result = 'PASS'
} catch {
    $report.error = $_.Exception.Message
    Write-Host "FAIL: $($report.error)"
} finally {
    if ($heldFile) { $heldFile.Dispose() }
    if ($junction -and (Test-Path -LiteralPath $junction)) {
        # Delete only the junction itself. Never recurse through its target.
        try { [IO.Directory]::Delete($junction) } catch { Write-Warning "Junction retained for inspection: $junction" }
    }
    if ($conflictShortcut -and (Test-Path -LiteralPath $conflictShortcut)) {
        try { if ((Hash $conflictShortcut) -eq $conflictHash) { [IO.File]::Delete($conflictShortcut) } } catch { }
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
