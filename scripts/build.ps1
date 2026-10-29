param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$Platform = 'x64',

    [string]$Project = 'magnifier.vcxproj'
)

$ErrorActionPreference = 'Stop'

$msbuild = $null

# 1) If msbuild is already on PATH, use it.
$cmd = Get-Command msbuild -ErrorAction SilentlyContinue
if ($cmd) {
    $msbuild = $cmd.Source
}

# 2) Otherwise, try to locate it via vswhere (installed with Visual Studio / Build Tools).
if (-not $msbuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $msbuild = (& $vswhere -latest -products * -requires 'Microsoft.Component.MSBuild' -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1)
    }
}

if (-not $msbuild) {
    Write-Error 'MSBuild.exe not found. Install Visual Studio Build Tools (MSVC + Windows SDK) or Visual Studio.'
    exit 1
}

& $msbuild $Project "/p:Configuration=$Configuration" "/p:Platform=$Platform"
exit $LASTEXITCODE
