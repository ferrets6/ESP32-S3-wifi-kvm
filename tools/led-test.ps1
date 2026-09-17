<#
.SYNOPSIS
  Flashes a GPIO pin on the board's onboard RGB LED via the "lt:<gpio>"
  diagnostic WebSocket command (see main.cpp), so the correct pin for the
  onboard LED can be found by trial without reflashing the firmware.

.EXAMPLE
  ./tools/led-test.ps1 -Pin 48
  ./tools/led-test.ps1 -Pin 38 -BoardHost 192.168.6.50
#>
param(
  [Parameter(Mandatory = $true)][int]$Pin,
  [string]$BoardHost = "192.168.6.50"
)

$ws = New-Object System.Net.WebSockets.ClientWebSocket
$cts = New-Object System.Threading.CancellationTokenSource
$cts.CancelAfter(5000)

$ws.ConnectAsync([Uri]"ws://$BoardHost/ws", $cts.Token).Wait()
$bytes = [System.Text.Encoding]::UTF8.GetBytes("lt:$Pin")
$seg = New-Object System.ArraySegment[byte] (, $bytes)
$ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $cts.Token).Wait()
Write-Output "Test sent on GPIO$Pin. Watch the board now (~300ms flash)."
Start-Sleep -Milliseconds 600
$ws.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "done", $cts.Token).Wait()
