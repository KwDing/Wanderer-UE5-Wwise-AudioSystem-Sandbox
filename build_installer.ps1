[CmdletBinding()]
param(
    [Alias('v')][ValidatePattern('^\d{8}-\d{6}-\d{3}$')][string]$Version,
    [string]$ISCC = $env:INNO_ISCC,
    [switch]$SingleExe,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$releaseRoot = Join-Path $PSScriptRoot 'Wanderer/Saved/Releases'
$template = Join-Path $PSScriptRoot 'Wanderer.iss'

function Get-SHA256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Test-Release([string]$Folder) {
    $windows = Join-Path $Folder 'Windows'
    foreach ($name in @('Wanderer.exe', 'Wanderer/Binaries/Win64/Wanderer-Win64-Shipping.exe')) {
        $file = Get-Item -LiteralPath (Join-Path $windows $name) -ErrorAction SilentlyContinue
        if (!$file -or $file.Length -le 0) { return $false }
    }
    $paks = Join-Path $windows 'Wanderer/Content/Paks'
    foreach ($extension in 'pak', 'utoc', 'ucas') {
        $files = @(Get-ChildItem -LiteralPath $paks -Filter "*.$extension" -File -ErrorAction SilentlyContinue |
            Where-Object Length -gt 0)
        if (!$files.Count) { return $false }
    }
    return $true
}

try {
    if (!(Test-Path -LiteralPath $template -PathType Leaf)) {
        throw "Installer template missing: $template"
    }
    if (!(Test-Path -LiteralPath $releaseRoot -PathType Container)) {
        throw "Windows Shipping release directory missing: $releaseRoot"
    }
    if ($Version) {
        $release = Get-Item -LiteralPath (Join-Path $releaseRoot $Version)
        if (!(Test-Release $release.FullName)) { throw "Incomplete release: $Version" }
    } else {
        $release = Get-ChildItem -LiteralPath $releaseRoot -Directory |
            Where-Object { $_.Name -match '^\d{8}-\d{6}-\d{3}$' } |
            Sort-Object Name -Descending |
            Where-Object { Test-Release $_.FullName } | Select-Object -First 1
        if (!$release) { throw 'No complete Windows Shipping release found.' }
        $Version = $release.Name
    }
    $source = Join-Path $release.FullName 'Windows'
    $files = @(Get-ChildItem -LiteralPath $source -Recurse -File)
    $bytes = ($files | Measure-Object Length -Sum).Sum
    $output = Join-Path $releaseRoot "Installers/$Version"
    $plan = [ordered]@{ Version=$Version; Source=$source; Output=$output; Files=$files.Count;
        SourceBytes=$bytes; SingleExe=[bool]$SingleExe; Signed=$false }
    $plan | ConvertTo-Json
    if ($DryRun) { exit 0 }
    if (!$ISCC) {
        $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($compiler) { $ISCC = $compiler.Source }
        else {
            $candidates = @(
                (Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6/ISCC.exe'),
                (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe')
            )
            $ISCC = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        }
    }
    if (!$ISCC -or !(Test-Path -LiteralPath $ISCC -PathType Leaf)) {
        throw 'Inno Setup 6 compiler missing. Install it separately, then pass -ISCC or set INNO_ISCC.'
    }
    # A unique output prevents old .bin slices from mixing with a rebuilt installer.
    $output = Join-Path $output (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $arguments = @("/DReleaseDir=$source", "/DReleaseVersion=$Version", "/DOutputDir=$output")
    if ($SingleExe) { $arguments += '/DSingleExe=1' }
    & $ISCC @arguments $template
    if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed ($LASTEXITCODE). No release is ready." }
    $artifacts = @(Get-ChildItem -LiteralPath $output -File | Where-Object Extension -in '.exe','.bin')
    if (!($artifacts | Where-Object Extension -eq '.exe')) { throw 'Compiler produced no installer executable.' }
    $hashes = foreach ($file in $artifacts) {
        '{0}  {1}' -f (Get-SHA256 $file.FullName), $file.Name
    }
    $hashes | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
    $plan.Output = $output
    $plan | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'build-info.json') -Encoding UTF8
    if ($artifacts | Where-Object Length -ge 2GB) {
        Write-Warning 'An artifact reaches the GitHub 2 GiB asset limit. Rebuild without -SingleExe or use another host.'
    }
    Write-Host "Installer ready: $output (keep the EXE and every BIN together)."
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
