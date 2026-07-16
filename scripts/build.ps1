[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\output'),
    [string]$Version
)

$ErrorActionPreference = 'Stop'
if ($Version -and $Version -notmatch '^\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?$') {
    throw "Version '$Version' is not a valid semantic version."
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outputRoot = [IO.Path]::GetPathRoot($OutputDirectory)
if ($OutputDirectory -eq $root -or $OutputDirectory -eq $outputRoot) {
    throw "Refusing to clean unsafe output directory '$OutputDirectory'."
}

if (Test-Path -LiteralPath $OutputDirectory) {
    Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$nativeProject = Join-Path $root 'src\WinProcessProxy.Native\WinProcessProxy.Native.vcxproj'
$nativeTestProject = Join-Path $root 'tests\WinProcessProxy.Native.Tests\WinProcessProxy.Native.Tests.vcxproj'
$serviceProject = Join-Path $root 'src\WinProcessProxy.Service\WinProcessProxy.Service.csproj'
$testProject = Join-Path $root 'tests\WinProcessProxy.Service.Tests\WinProcessProxy.Service.Tests.csproj'

$msbuild = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($installationPath) {
        $msbuild = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
    }
}

if (-not $msbuild) {
    $searchPatterns = @(
        'C:\Program Files\Microsoft Visual Studio\*\*\MSBuild\Current\Bin\MSBuild.exe',
        'D:\Program Files\Microsoft Visual Studio\*\*\MSBuild\Current\Bin\MSBuild.exe'
    )

    $msbuild = Get-Item -Path $searchPatterns -ErrorAction SilentlyContinue |
        Where-Object {
            $msbuildRoot = Split-Path (Split-Path (Split-Path $_.FullName -Parent) -Parent) -Parent
            Test-Path (Join-Path $msbuildRoot 'Microsoft\VC')
        } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

if (-not $msbuild) {
    throw 'MSVC x64 build tools were not found. Install Visual Studio with the Desktop development with C++ workload.'
}

& $msbuild $nativeProject /restore /m /p:Configuration=$Configuration /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }

& $msbuild $nativeTestProject /restore /m /p:Configuration=$Configuration /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'Native test build failed.' }

$nativeTestExecutable = Join-Path (Split-Path $nativeTestProject) "bin\$Configuration\WinProcessProxy.Native.Tests.exe"
& $nativeTestExecutable
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }

& dotnet test $testProject --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Managed tests failed.' }

$publishArguments = @(
    'publish',
    $serviceProject,
    '--configuration', $Configuration,
    '--runtime', 'win-x64',
    '--output', $OutputDirectory
)
if ($Version) {
    $publishArguments += "-p:Version=$Version"
}

& dotnet @publishArguments
if ($LASTEXITCODE -ne 0) { throw 'Native AOT publish failed.' }

Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.pdb' -File -Recurse |
    Remove-Item -Force

Copy-Item -Path (Join-Path $PSScriptRoot 'install-service.ps1') -Destination $OutputDirectory -Force
Copy-Item -Path (Join-Path $PSScriptRoot 'uninstall-service.ps1') -Destination $OutputDirectory -Force

Write-Host "Packaged WinProcessProxy.Native and WinProcessProxy.Service to $OutputDirectory"
