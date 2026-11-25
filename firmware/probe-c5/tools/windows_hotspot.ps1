param([switch]$Start)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Networking.Connectivity.NetworkInformation,Windows.Networking.Connectivity,ContentType=WindowsRuntime]
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]
$profile = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
if (!$profile) { throw 'Windows has no Internet connection profile for its mobile hotspot.' }
$manager = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($profile)
$config = $manager.GetCurrentAccessPointConfiguration()
if (!$config.Ssid -or !$config.Passphrase) { throw 'Windows mobile hotspot has no configured SSID/passphrase.' }
$local = Join-Path $PSScriptRoot '../.local'
New-Item -ItemType Directory -Path $local -Force | Out-Null
@{ssid=$config.Ssid; password=$config.Passphrase; band=[string]$config.Band; state=[string]$manager.TetheringOperationalState} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $local 'wifi.json') -Encoding UTF8
if ($Start -and $manager.TetheringOperationalState -ne 'On') {
    $operation = $manager.StartTetheringAsync()
    $asyncType = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]
    $asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name.StartsWith('IAsyncOperation')
    } | Select-Object -First 1
    $task = $asTask.MakeGenericMethod($asyncType).Invoke($null,@($operation))
    if (!$task.Wait(30000)) { throw 'Windows mobile hotspot start timed out.' }
    if ([string]$task.Result.Status -ne 'Success') { throw ('Windows hotspot start failed: ' + $task.Result.Status) }
}
if ($Start) {
    $readyDeadline = [DateTime]::UtcNow.AddSeconds(15)
    while ($manager.TetheringOperationalState -ne 'On' -and [DateTime]::UtcNow -lt $readyDeadline) { Start-Sleep -Milliseconds 250 }
    if ($manager.TetheringOperationalState -ne 'On') { throw ('Windows hotspot is not ready: ' + $manager.TetheringOperationalState) }
}
Write-Output ('Windows hotspot configuration saved locally; state=' + $manager.TetheringOperationalState + ', band=' + $config.Band)
