[CmdletBinding()]
param([string]$ServiceName = 'WinProcessProxy')

$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $powerShell = (Get-Process -Id $PID).Path
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', ('"{0}"' -f $PSCommandPath),
        '-ServiceName', ('"{0}"' -f $ServiceName)
    )
    $elevated = Start-Process -FilePath $powerShell -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $elevated.ExitCode
}

$service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if (-not $service) {
    Write-Host "Service '$ServiceName' is not installed."
    return
}

if ($service.Status -ne 'Stopped') {
    Stop-Service -Name $ServiceName
    $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
}

& sc.exe delete $ServiceName | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Could not delete service '$ServiceName'." }

for ($attempt = 0; $attempt -lt 30; $attempt++) {
    if (-not (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue)) {
        Write-Host "Service '$ServiceName' stopped and uninstalled."
        return
    }
    Start-Sleep -Milliseconds 200
}

throw "Service '$ServiceName' was marked for deletion but is still present."
