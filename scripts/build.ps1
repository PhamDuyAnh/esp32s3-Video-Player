param(
  [ValidateSet('selfTest', 'videoPlayer')][string]$Sketch = 'selfTest'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'board.ps1')
& (Join-Path $PSScriptRoot 'check-dependencies.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$setup = (Join-Path $repo 'firmware/videoPlayer/User_Setup_Xingzhi.h').Replace('\', '/')
$include = (Join-Path $repo 'firmware').Replace('\', '/')
$sketchPath = Join-Path $repo "firmware/$Sketch"
$output = Join-Path $repo ".build/$Sketch"
New-Item -ItemType Directory -Force $output | Out-Null

$flags = "compiler.cpp.extra_flags=-DUSER_SETUP_LOADED -include $setup -I$include"
& arduino-cli compile --fqbn $XingzhiFqbn --build-property $flags --output-dir $output $sketchPath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
