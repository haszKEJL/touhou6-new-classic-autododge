param([switch]$SkipBuild,[string]$BinaryDirectory='bin')
$ErrorActionPreference='Stop'
Set-Location -LiteralPath $PSScriptRoot
if(-not $SkipBuild) {
    $env:ASSIST_OUT=$BinaryDirectory
    & .\build.bat
    if($LASTEXITCODE -ne 0) { throw 'Build failed. Close the game before rebuilding a loaded DLL.' }
}
& (Join-Path $BinaryDirectory 'dodge_assist.exe') --self-test
if($LASTEXITCODE -ne 0) { throw 'Self-test failed.' }
$versionText=Get-Content -LiteralPath 'native\version.hpp' -Raw
if($versionText -notmatch 'version\[\]="([0-9.]+)"') { throw 'Version missing.' }
$version=$Matches[1]
$releaseRoot=Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
$stage=Join-Path $releaseRoot ('.package-'+[guid]::NewGuid().ToString('N'))
$bundle=Join-Path $stage 'ScarletAssist'
try {
    New-Item -ItemType Directory -Path (Join-Path $bundle 'bin'),(Join-Path $bundle 'licenses') -Force | Out-Null
    foreach($name in @('scarlet_launcher.exe','scarlet_assist.dll')) {
        Copy-Item -LiteralPath (Join-Path $BinaryDirectory $name) -Destination (Join-Path $bundle 'bin')
    }
    Copy-Item -LiteralPath 'run.bat','START_HERE.txt' -Destination $bundle
    Copy-Item -LiteralPath 'vendor\imgui\LICENSE.txt' -Destination (Join-Path $bundle 'licenses\Dear-ImGui.txt')
    Copy-Item -LiteralPath 'vendor\minhook\LICENSE.txt' -Destination (Join-Path $bundle 'licenses\MinHook.txt')
    $archive=Join-Path $releaseRoot "ScarletAssist-$version-Windows-x64.zip"
    Compress-Archive -LiteralPath $bundle -DestinationPath $archive -Force
    $hash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    Set-Content -LiteralPath ($archive+'.sha256') -Value ($hash+'  '+(Split-Path $archive -Leaf)) -Encoding ascii
    Write-Output $archive
} finally {
    if(Test-Path -LiteralPath $stage) {
        $resolved=(Resolve-Path -LiteralPath $stage).Path
        if((Split-Path $resolved -Parent) -ne (Resolve-Path -LiteralPath $releaseRoot).Path) { throw 'Invalid staging path.' }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
