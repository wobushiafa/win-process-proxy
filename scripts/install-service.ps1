[CmdletBinding()]
param(
    [string]$PublishDirectory = (Join-Path $PSScriptRoot '..\output'),
    [string]$ServiceName = 'WinProcessProxy'
)

$ErrorActionPreference = 'Stop'
$PublishDirectory = [IO.Path]::GetFullPath($PublishDirectory)
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $powerShell = (Get-Process -Id $PID).Path
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', ('"{0}"' -f $PSCommandPath),
        '-PublishDirectory', ('"{0}"' -f $PublishDirectory),
        '-ServiceName', ('"{0}"' -f $ServiceName)
    )
    $elevated = Start-Process -FilePath $powerShell -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $elevated.ExitCode
}

$requiredFiles = @(
    'WinProcessProxy.Service.exe',
    'WinProcessProxy.Native.dll',
    'nfapi.dll',
    'nfdriver.sys',
    'appsettings.json'
)
foreach ($file in $requiredFiles) {
    if (-not (Test-Path (Join-Path $PublishDirectory $file) -PathType Leaf)) {
        throw "Required package file is missing: $file"
    }
}

$executable = Join-Path $PublishDirectory 'WinProcessProxy.Service.exe'
$binaryPath = '"{0}"' -f $executable
$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue

if ($service) {
    if ($service.Status -ne 'Stopped') {
        Stop-Service -Name $ServiceName
        $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
    }

    & sc.exe config $ServiceName binPath= $binaryPath start= auto DisplayName= 'WinProcessProxy Network Service' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Could not update service '$ServiceName'." }
}
else {
    New-Service -Name $ServiceName `
        -BinaryPathName $binaryPath `
        -DisplayName 'WinProcessProxy Network Service' `
        -Description 'Redirects Windows network traffic through a configured SOCKS5 proxy.' `
        -StartupType Automatic
}

& sc.exe description $ServiceName 'Redirects Windows network traffic through a configured SOCKS5 proxy.' | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Could not set the description for service '$ServiceName'." }

& sc.exe failure $ServiceName reset= 86400 actions= restart/5000/restart/15000/none/0 | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Could not set recovery actions for service '$ServiceName'." }

Start-Service -Name $ServiceName
Write-Host "Service '$ServiceName' installed and started from '$PublishDirectory'."
