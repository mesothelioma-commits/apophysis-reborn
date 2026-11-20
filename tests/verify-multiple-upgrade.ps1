# Run only in a disposable Windows user profile with no existing Apophysis installations.
param([Parameter(Mandatory)][string]$Installer, [Parameter(Mandatory)][string]$Work)
$ErrorActionPreference = 'Stop'
$root = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
if ((Get-ChildItem $root -ErrorAction SilentlyContinue | Where-Object PSChildName -like 'ApophysisReborn-*') -or (Test-Path $Work)) {
    throw 'This test requires a fresh profile and work folder.'
}
New-Item -Type Directory $Work | Out-Null
$old = [ordered]@{
    '0.15.0' = 'a346d5b34378d0af6a0674c28e0abfc200dd2cb1e14710dcb42208ed79962cf9'
    '0.14.0' = '82b4602e74f99b5056154f4b8b52bcb43f6a93c4e72765561e79f12296a84bca'
}
function Install([string]$Exe, [string]$Version) {
    $destination = [IO.Path]::GetFullPath((Join-Path $Work $Version))
    $result = Start-Process $Exe -ArgumentList "/S /D=$destination" -Wait -PassThru
    if ($result.ExitCode -ne 0) {
        Get-ChildItem $env:TEMP -Filter '*.txt' | Where-Object LastWriteTime -gt (Get-Date).AddMinutes(-5) | ForEach-Object {
            if (Select-String -LiteralPath $_.FullName -SimpleMatch 'Apophysis' -Quiet) { Get-Content -LiteralPath $_.FullName -Tail 12 }
        }
        throw "Setup $Version exited $($result.ExitCode)"
    }
    if (!(Test-Path "$root/ApophysisReborn-$Version")) { throw "Registration missing for $Version" }
}
foreach ($v in $old.Keys) {
    $file = Join-Path $Work "old-$v.exe"
    $url = "https://github.com/mesothelioma-commits/apophysis-reborn/releases/download/v$v/apophysis-reborn-$v-windows-x64-setup.exe"
    Invoke-WebRequest $url -OutFile $file
    if ((Get-FileHash $file).Hash -ne $old[$v]) { throw "Unexpected setup for $v" }
    Install $file $v
    Set-Content -LiteralPath "$Work/$v/personal.apoproject" -Value "Keep $v"
}
foreach ($v in $old.Keys) {
    if (!(Test-Path "$root/ApophysisReborn-$v")) { throw 'Two genuine previous versions must coexist' }
}
Install $Installer '0.17.0'
foreach ($v in $old.Keys) {
    if (Test-Path "$root/ApophysisReborn-$v") { throw "Stale registration for $v" }
    $left = @(Get-ChildItem "$Work/$v" -Recurse -File -Force)
    if ($left.Count -ne 1 -or $left[0].Name -ne 'personal.apoproject' -or (Get-Content $left[0].FullName) -ne "Keep $v") {
        throw "Incorrect cleanup in $v"
    }
    $menu = [Environment]::GetFolderPath('Programs')
    if (Test-Path "$menu/Apophysis Reborn/Apophysis Reborn $v.lnk") { throw "Old shortcut retained: $v" }
}
$version = & "$Work/0.17.0/flame-cpu.exe" --version
if ($LASTEXITCODE -ne 0 -or "$version" -notmatch '0\.17\.0') { throw 'Installed renderer failed version check' }
Write-Host "Two prior installations removed, personal files preserved, renderer started: $version"
Get-FileHash $Installer | Format-List
