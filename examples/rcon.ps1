# Sends one command to csgo_gc's RCON (Source RCON protocol) and prints the answer.
#   .\rcon.ps1 "give_stars 500"
#   .\rcon.ps1 "help"
# RCON must be on: csgo_gc\config.txt, "rcon" { "enabled" "1" ... }, then restart the game.
param(
    [Parameter(Mandatory = $true)][string]$Command,
    [string]$Server = "127.0.0.1",
    [int]$Port = 37016,
    [string]$Password = ""
)

function Send-Packet($stream, [int]$id, [int]$type, [string]$body) {
    $text = [System.Text.Encoding]::UTF8.GetBytes($body)
    $ms = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter($ms)
    $w.Write([int](10 + $text.Length))
    $w.Write([int]$id)
    $w.Write([int]$type)
    $w.Write($text)
    $w.Write([byte]0)
    $w.Write([byte]0)
    $w.Flush()
    $bytes = $ms.ToArray()
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
}

function Read-Exact($stream, [int]$count) {
    $buffer = New-Object byte[] $count
    $got = 0
    while ($got -lt $count) {
        $n = $stream.Read($buffer, $got, $count - $got)
        if ($n -le 0) { throw "connection closed" }
        $got += $n
    }
    return ,$buffer
}

function Read-Packet($stream) {
    $size = [System.BitConverter]::ToInt32((Read-Exact $stream 4), 0)
    $data = Read-Exact $stream $size
    return @{
        Id   = [System.BitConverter]::ToInt32($data, 0)
        Type = [System.BitConverter]::ToInt32($data, 4)
        Body = [System.Text.Encoding]::UTF8.GetString($data, 8, [Math]::Max(0, $size - 10))
    }
}

try {
    $client = New-Object System.Net.Sockets.TcpClient
    $client.ReceiveTimeout = 5000
    $client.Connect($Server, $Port)
    $stream = $client.GetStream()

    Send-Packet $stream 1 3 $Password
    $auth = Read-Packet $stream
    if ($auth.Type -eq 0) { $auth = Read-Packet $stream }   # some servers send an empty packet first
    if ($auth.Id -eq -1) { Write-Host "RCON refused the password"; exit 1 }

    Send-Packet $stream 2 2 $Command
    $reply = Read-Packet $stream
    Write-Host $reply.Body
    $client.Close()
}
catch {
    Write-Host "Could not talk to RCON at ${Server}:${Port}: $_"
    Write-Host "Is the game running with rcon enabled in csgo_gc\config.txt?"
    exit 1
}
