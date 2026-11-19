# Windows PowerShell 5.1. The caller supplies native paths and a UTF-8 argument file.
param([Parameter(Mandatory=$true)][string]$Program,
      [Parameter(Mandatory=$true)][string]$ArgumentFile,
      [Parameter(Mandatory=$true)][string]$Log,
      [Parameter(Mandatory=$true)][string]$WorkingDirectory,
      [Parameter(Mandatory=$true)][int]$LimitSeconds)
$ErrorActionPreference = 'Stop'
function Quote-NativeArgument([string]$Value) {
    # Windows CommandLineToArgvW / C runtime rules for quotes and trailing backslashes.
    $s = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $s = [regex]::Replace($s, '(\\+)$', '$1$1')
    return '"' + $s + '"'
}
$code = 125
$child = $null
$started = $false
$stdout = $null
$stderr = $null
$errFile = $Log + '.stderr'
$jsonOutput = $false
try {
    $arguments = [IO.File]::ReadAllText($ArgumentFile, [Text.Encoding]::UTF8)
    $quoted = @()
    $jsonOutput = $false
    if ($arguments.Length -gt 0) {
        if ($arguments[$arguments.Length - 1] -ne [char]0) { throw 'Incomplete argument file' }
        $arguments = $arguments.Substring(0, $arguments.Length - 1)
        foreach ($value in $arguments.Split([char]0)) {
            if ($value -ceq '--json') { $jsonOutput = $true }
            $quoted += (Quote-NativeArgument $value)
        }
    }
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $Program
    $start.Arguments = $quoted -join ' '
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $child = New-Object Diagnostics.Process
    $child.StartInfo = $start
    $stdout = [IO.File]::Create($Log)
    $stderr = [IO.File]::Create($errFile)
    $started = $child.Start()
    if (-not $started) { throw 'Process did not start' }
    $outTask = $child.StandardOutput.BaseStream.CopyToAsync($stdout)
    $errTask = $child.StandardError.BaseStream.CopyToAsync($stderr)
    if (-not $child.WaitForExit($LimitSeconds * 1000)) {
        if (-not $child.HasExited) { $child.Kill() }
        if (-not $child.WaitForExit(5000)) { throw 'Test process did not stop within five seconds' }
        $code = 124
    } else {
        $code = $child.ExitCode
        if ($code -lt 0 -or $code -gt 255) {
            [Console]::Error.WriteLine("Windows process exit code: $code")
            $code = 1
        }
    }
    if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($outTask,$errTask),5000)) {
        throw 'The test process output did not close'
    }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    $code = 125
} finally {
    if ($started -and -not $child.HasExited) {
        try { $child.Kill(); [void]$child.WaitForExit(5000) } catch {}
    }
    if ($null -ne $stdout) { $stdout.Dispose() }
    if ($null -ne $stderr) { $stderr.Dispose() }
    if ($null -ne $child) { $child.Dispose() }
}
if ((Test-Path -LiteralPath $errFile) -and (Get-Item -LiteralPath $errFile).Length -gt 0) {
    if ($jsonOutput) {
        [IO.File]::Move($errFile, [IO.Path]::ChangeExtension($Log, 'stderr.txt'))
    } else {
        $dest = [IO.File]::Open($Log, [IO.FileMode]::Append)
        $src = [IO.File]::OpenRead($errFile)
        try {
            if ($dest.Length -gt 0) { $dest.WriteByte(10) }
            $src.CopyTo($dest)
        } finally { $src.Dispose(); $dest.Dispose() }
    }
}
if (Test-Path -LiteralPath $errFile) { [IO.File]::Delete($errFile) }
if ($code -eq 124) { [IO.File]::AppendAllText($Log, "`r`nTimeout after $LimitSeconds seconds.`r`n") }
exit $code
