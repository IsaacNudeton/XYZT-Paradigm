$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 3000
$port.DtrEnable = $true
$port.Open()
Start-Sleep -Milliseconds 2500

# Flush everything
while ($port.BytesToRead -gt 0) {
    $port.ReadExisting() | Out-Null
    Start-Sleep -Milliseconds 100
}

# Send blank line to clear any partial input
$port.Write("`r")
Start-Sleep -Milliseconds 300
while ($port.BytesToRead -gt 0) {
    $port.ReadExisting() | Out-Null
    Start-Sleep -Milliseconds 100
}

# Send id command
$port.Write("id`r")
Start-Sleep -Milliseconds 1500
$resp = ''
while ($port.BytesToRead -gt 0) {
    $resp += $port.ReadExisting()
    Start-Sleep -Milliseconds 100
}
Write-Output $resp

# Send pins command
$port.Write("pins`r")
Start-Sleep -Milliseconds 1000
$resp2 = ''
while ($port.BytesToRead -gt 0) {
    $resp2 += $port.ReadExisting()
    Start-Sleep -Milliseconds 100
}
Write-Output $resp2

$port.Close()
