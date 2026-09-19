param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^[A-Za-z]:\\?$')]
  [string]$Destination
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = Join-Path $repo 'videoConverter/output_sd'
$root = [IO.Path]::GetPathRoot($Destination)
$drive = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$($root.TrimEnd('\'))'"
if (-not $drive) { throw "Destination drive not found: $root" }
if ($drive.DriveType -ne 2) {
  throw "Destination must be a removable drive (DriveType 2): $root"
}

$files = Get-ChildItem -LiteralPath $source -File |
  Where-Object { $_.Extension -in '.mjpeg', '.wav' } |
  Sort-Object Name
if (-not $files) { throw "No media files found in $source" }

$required = ($files | Measure-Object Length -Sum).Sum
if ($drive.FreeSpace -lt $required) {
  throw "Not enough free space: need $required bytes, available $($drive.FreeSpace) bytes"
}

foreach ($file in $files) {
  $target = Join-Path $root $file.Name
  Write-Host "Copying $($file.Name) ($($file.Length) bytes)..."
  Copy-Item -LiteralPath $file.FullName -Destination $target -Force
  $sourceHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
  $targetHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
  if ($sourceHash -ne $targetHash) { throw "Checksum mismatch: $($file.Name)" }
  Write-Host "  verified $sourceHash"
}
Write-Host "Copied and verified $($files.Count) files to $root. Eject the card safely."
