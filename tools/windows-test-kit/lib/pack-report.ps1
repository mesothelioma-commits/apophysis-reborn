param([Parameter(Mandatory=$true)][string]$RunDirectory,
      [Parameter(Mandatory=$true)][string]$OutputZip)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$run = [IO.DirectoryInfo]$RunDirectory
$root = $run.FullName.TrimEnd([char]92,[char]47)
$files = @('report.txt','results.tsv','run-log.txt','timings.tsv') | ForEach-Object {
    Get-Item -LiteralPath ([IO.Path]::Combine($root,$_))
}
$logs = [IO.Path]::Combine($root,'logs')
$files += @(Get-ChildItem -LiteralPath $logs -File -Filter '*.txt')
foreach ($relative in @('editor/support/support-report.json','editor/support/system-information.json','editor/queue/queue-report.json','editor/animation/animation-report.json')) {
    $candidate = [IO.Path]::Combine($root,$relative)
    if ([IO.File]::Exists($candidate)) { $files += Get-Item -LiteralPath $candidate }
}
$zip = [IO.Compression.ZipFile]::Open($OutputZip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($item in $files) {
        $name = $item.FullName.Substring($root.Length + 1).Replace([char]92,[char]47)
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$item.FullName,$name,[IO.Compression.CompressionLevel]::Optimal)
    }
} finally { $zip.Dispose() }
$check = [IO.Compression.ZipFile]::OpenRead($OutputZip)
try {
    if ($check.Entries.Count -ne $files.Count -or $null -eq $check.GetEntry('report.txt')) { throw 'Result ZIP inventory mismatch' }
} finally { $check.Dispose() }
