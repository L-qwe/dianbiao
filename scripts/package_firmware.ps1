$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pioRoot = if ($env:PLATFORMIO_CORE_DIR) { $env:PLATFORMIO_CORE_DIR } else { Join-Path $env:USERPROFILE '.platformio' }
$pioPython = Join-Path $pioRoot 'penv/Scripts/python.exe'
$esptool = Join-Path $pioRoot 'packages/tool-esptoolpy/esptool.py'
$bootApp = Join-Path $pioRoot 'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
$versionSource = Get-Content -LiteralPath (Join-Path $root 'src/app_config.cpp') -Raw
$version = [regex]::Match($versionSource, 'FIRMWARE_VERSION\s*=\s*"([^"]+)"').Groups[1].Value
if (-not $version) { throw 'Firmware version missing.' }
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$manifest = @()
foreach ($target in @(@{Env='esp32-s3-devkitc-1'; Flash='16MB'})) {
    $build = Join-Path $root ".pio/build/$($target.Env)"
    $app = Join-Path $build 'firmware.bin'
    $boot = Join-Path $build 'bootloader.bin'
    $partitions = Join-Path $build 'partitions.bin'
    foreach ($file in @($app, $boot, $partitions, $bootApp)) {
        if (-not (Test-Path -LiteralPath $file)) { throw "Missing $file; build $($target.Env) first." }
    }
    $inputs = Get-ChildItem -LiteralPath (Join-Path $root 'src'),(Join-Path $root 'include'),(Join-Path $root 'web') -File -Recurse
    $stale = $inputs | Where-Object { $_.LastWriteTimeUtc -gt (Get-Item -LiteralPath $app).LastWriteTimeUtc }
    if ($stale) { throw "Source changed after $($target.Env) build; rebuild before packaging." }
    $base = "DianBiao_${version}_$($target.Flash)"
    $ota = Join-Path $dist "${base}_ota.bin"
    $factory = Join-Path $dist "${base}_factory.bin"
    Copy-Item -LiteralPath $app -Destination $ota
    & $pioPython $esptool --chip esp32s3 merge_bin --flash_mode qio --flash_freq 80m --flash_size $target.Flash -o $factory 0x0 $boot 0x8000 $partitions 0xe000 $bootApp 0x10000 $app
    if ($LASTEXITCODE) { throw "Packaging failed: $($target.Env)" }
    foreach ($file in @($ota, $factory)) {
        $manifest += [ordered]@{
            file = Split-Path $file -Leaf
            environment = $target.Env
            version = $version
            bytes = (Get-Item -LiteralPath $file).Length
            md5 = (Get-FileHash -LiteralPath $file -Algorithm MD5).Hash.ToLowerInvariant()
            sha256 = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
            purpose = if ($file -eq $ota) { 'OTA application only' } else { 'Initial serial flash at 0x0' }
        }
    }
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $dist 'manifest.json') -Encoding utf8
Write-Output "Firmware packaged: $dist"
