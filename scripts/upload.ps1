param(
  [ValidateSet('selfTest', 'videoPlayer')][string]$Sketch = 'selfTest',
  [string]$Port = 'COM9'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'board.ps1')
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo ".build/$Sketch"
$binary = Join-Path $output "$Sketch.ino.bin"
if (-not (Test-Path -LiteralPath $binary)) { throw "Build first: $binary" }
& arduino-cli upload --port $Port --fqbn $XingzhiFqbn --input-dir $output --verify (Join-Path $repo "firmware/$Sketch")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
