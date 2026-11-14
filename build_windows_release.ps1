[CmdletBinding()]
param([string]$EngineRoot = $env:UE_ENGINE_ROOT, [switch]$NoLaunch)

$ErrorActionPreference = 'Stop'
$game = Join-Path $PSScriptRoot 'Wanderer'
$project = Join-Path $game 'Wanderer.uproject'
$releaseRoot = Join-Path $game 'Saved\Releases'
$archive = Join-Path $releaseRoot (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
$log = "$archive.log"

function Get-SHA256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

try {
    if (!$EngineRoot) {
        $install = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.5' -ErrorAction SilentlyContinue
        $EngineRoot = $install.InstalledDirectory
        if (!$EngineRoot) { $EngineRoot = 'D:\Apps\UE\UE_5.5' }
    }
    $uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
    if (!(Test-Path -LiteralPath $uat)) { throw "UE 5.5 RunUAT.bat missing. Set UE_ENGINE_ROOT to your UE 5.5 install." }

    $releaseFx = Join-Path $game 'Plugins\WwiseSoundEngine\ThirdParty\x64_vc170\Release\bin\Crossfeed.dll'
    $bank = Join-Path $game 'Wanderer_WwiseProject\GeneratedSoundBanks\Windows\Init.bnk'
    foreach ($required in @($project, $releaseFx, $bank)) {
        if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required input: $required" }
    }

    New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
    Write-Host "Building Windows Shipping. Log: $log"
    $arguments = @('BuildCookRun', "-project=$project", '-target=Wanderer', '-platform=Win64',
        '-clientconfig=Shipping', '-map=/Game/Maps/L_AudioSystemDemo', '-build', '-cook', '-stage',
        '-pak', '-iostore', '-compressed', '-package', '-prereqs', '-nodebuginfo', '-archive',
        "-archivedirectory=$archive", '-nop4', '-unattended', '-utf8output')
    & $uat @arguments | Tee-Object -FilePath $log
    if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE). See $log" }

    $windows = Join-Path $archive 'Windows'
    $exe = Join-Path $windows 'Wanderer.exe'
    $paks = Join-Path $windows 'Wanderer\Content\Paks'
    foreach ($file in @($exe, (Join-Path $windows 'Wanderer\Binaries\Win64\Wanderer-Win64-Shipping.exe'))) {
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing packaged executable: $file" }
    }
    foreach ($extension in 'pak','utoc','ucas') {
        if (!(Get-ChildItem -LiteralPath $paks -Filter "*.$extension" -File | Where-Object Length -gt 0)) {
            throw "Missing packaged .$extension data in $paks"
        }
    }
    $fx = @(Get-ChildItem -LiteralPath $windows -Recurse -File -Filter Crossfeed.dll)
    if ($fx.Count -ne 1 -or (Get-SHA256 $fx[0].FullName) -ne (Get-SHA256 $releaseFx)) {
        throw "Packaged Crossfeed.dll missing, ambiguous, or different from the Release DLL."
    }
    Write-Host "Ready to play: $exe"
    if (!$NoLaunch) { Start-Process -FilePath $exe -WorkingDirectory $windows }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
