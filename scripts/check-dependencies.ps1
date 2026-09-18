$ErrorActionPreference = 'Stop'
$platforms = (arduino-cli core list --format json | ConvertFrom-Json).platforms
$core = $platforms | Where-Object { $_.id -eq 'esp32:esp32' }
if ($core.installed_version -ne '2.0.17') {
  throw "Expected Arduino-ESP32 2.0.17, found $($core.installed_version)"
}
$libraries = (arduino-cli lib list --format json | ConvertFrom-Json).installed_libraries
$required = @{
  'TFT_eSPI' = '2.5.44'
  'JPEGDEC' = '1.2.8'
  'ESP32-audioI2S-master' = '2.0.0'
}
foreach ($name in $required.Keys) {
  $installed = $libraries | Where-Object { $_.library.name -eq $name }
  if (-not $installed -or $installed.library.version -ne $required[$name]) {
    throw "Expected $name $($required[$name]); check installed Arduino libraries"
  }
}
Write-Host 'Dependency versions match the verified build set.'
