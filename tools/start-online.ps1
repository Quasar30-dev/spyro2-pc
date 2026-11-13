$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOnline = Join-Path $taskRoot 'data/online'
New-Item -ItemType Directory -Path $taskOnline -Force | Out-Null
$taskPython = (Get-Command python -ErrorAction Stop).Source
$taskCloudflare = Join-Path $taskRoot 'tools/bin/cloudflared.exe'
if (-not (Test-Path -LiteralPath $taskCloudflare)) { throw 'cloudflared.exe is missing from tools/bin.' }
$taskStatePath = Join-Path $taskOnline 'processes.json'
if (Test-Path -LiteralPath $taskStatePath) {
    $taskExisting = Get-Content -LiteralPath $taskStatePath -Raw | ConvertFrom-Json
    $taskRelayAlive = Get-Process -Id $taskExisting.relay -ErrorAction SilentlyContinue
    $taskTunnelAlive = Get-Process -Id $taskExisting.tunnel -ErrorAction SilentlyContinue
    if ($taskRelayAlive -and $taskTunnelAlive) {
        Get-Content -LiteralPath (Join-Path $taskOnline 'relay-url.txt')
        return
    }
    throw 'A previous relay stopped partially. Run Stop Online Relay.cmd before restarting.'
}
$taskRelayScript = Join-Path $taskRoot 'tools/online_relay.py'
$taskRelay = Start-Process -FilePath $taskPython -ArgumentList @('-u', ('"' + $taskRelayScript + '"')) -WindowStyle Hidden -PassThru -WorkingDirectory $taskRoot -RedirectStandardOutput (Join-Path $taskOnline 'relay.log') -RedirectStandardError (Join-Path $taskOnline 'relay-error.log')
try {
    $taskReady = $false
    for ($taskAttempt=0; $taskAttempt -lt 20; $taskAttempt++) {
        Start-Sleep -Milliseconds 200
        try { $taskCheck = Invoke-WebRequest -Uri 'http://127.0.0.1:27820/health' -TimeoutSec 1 -UseBasicParsing; $taskReady = $taskCheck.StatusCode -eq 200 } catch {}
        if ($taskReady) { break }
    }
    if (-not $taskReady) { throw 'The local relay could not start.' }
    $taskTunnel = Start-Process -FilePath $taskCloudflare -ArgumentList @('tunnel','--no-autoupdate','--url','http://127.0.0.1:27820','--protocol','http2') -WindowStyle Hidden -PassThru -WorkingDirectory $taskRoot -RedirectStandardOutput (Join-Path $taskOnline 'tunnel.log') -RedirectStandardError (Join-Path $taskOnline 'tunnel-error.log')
    $taskUrl = $null
    for ($taskAttempt=0; $taskAttempt -lt 80; $taskAttempt++) {
        Start-Sleep -Milliseconds 500
        $taskLog = Get-Content -LiteralPath (Join-Path $taskOnline 'tunnel-error.log') -Raw -ErrorAction SilentlyContinue
        if ($taskLog -match 'https://[a-z0-9-]+\.trycloudflare\.com') { $taskUrl = $Matches[0]; break }
        if ($taskTunnel.HasExited) { throw 'The temporary Cloudflare tunnel stopped. See data/online/tunnel-error.log.' }
    }
    if (-not $taskUrl) { throw 'Cloudflare did not provide a temporary URL.' }
    $taskUrl | Set-Content -LiteralPath (Join-Path $taskOnline 'relay-url.txt') -Encoding ASCII
    [pscustomobject]@{relay=$taskRelay.Id;relayStart=$taskRelay.StartTime.ToUniversalTime().ToString('o');tunnel=$taskTunnel.Id;tunnelStart=$taskTunnel.StartTime.ToUniversalTime().ToString('o');url=$taskUrl} | ConvertTo-Json | Set-Content -LiteralPath $taskStatePath
    Write-Output $taskUrl
} catch {
    if ($taskTunnel -and -not $taskTunnel.HasExited) { Stop-Process -Id $taskTunnel.Id }
    if (-not $taskRelay.HasExited) { Stop-Process -Id $taskRelay.Id }
    throw
}
