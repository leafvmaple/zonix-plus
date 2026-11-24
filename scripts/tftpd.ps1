# Minimal read-only TFTP server (RFC 1350 + blksize/tsize options) for
# loading Zonix onto the VisionFive 2 from U-Boot. Serves files from -Root.
# One transfer at a time; replies from port 69.
param([Parameter(Mandatory)][string]$Root, [int]$Port = 69)

$ErrorActionPreference = 'Stop'
$udp = New-Object System.Net.Sockets.UdpClient $Port
$udp.Client.ReceiveTimeout = 1000
Write-Host "tftpd: serving $Root on UDP $Port"

function Send-Packet([byte[]]$bytes, $ep) { [void]$udp.Send($bytes, $bytes.Length, $ep) }

function Send-Error([int]$code, [string]$msg, $ep) {
    $m = [System.Text.Encoding]::ASCII.GetBytes($msg)
    $p = New-Object byte[] (5 + $m.Length)
    $p[1] = 5; $p[3] = [byte]$code
    [Array]::Copy($m, 0, $p, 4, $m.Length)
    Send-Packet $p $ep
}

function Receive-From($expectedEp) {
    $any = New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Any), 0
    try { $d = $udp.Receive([ref]$any) } catch [System.Net.Sockets.SocketException] { return $null }
    if ($expectedEp -and -not $any.Equals($expectedEp)) { return @{ Data = $d; Ep = $any; Stranger = $true } }
    return @{ Data = $d; Ep = $any; Stranger = $false }
}

# Sends a packet and waits for ACK of block number $block; returns $true on success.
function Send-Reliable([byte[]]$packet, [int]$block, $ep) {
    for ($try = 0; $try -lt 8; $try++) {
        Send-Packet $packet $ep
        $deadline = (Get-Date).AddSeconds(1)
        while ((Get-Date) -lt $deadline) {
            $r = Receive-From $ep
            if (-not $r -or $r.Stranger) { continue }
            $d = $r.Data
            if ($d.Length -ge 4 -and $d[1] -eq 4 -and (([int]$d[2] -shl 8) -bor [int]$d[3]) -eq ($block -band 0xFFFF)) { return $true }
            if ($d.Length -ge 2 -and $d[1] -eq 5) { return $false }
        }
    }
    return $false
}

while ($true) {
    $r = Receive-From $null
    if (-not $r) { continue }
    $d = $r.Data; $ep = $r.Ep
    if ($d.Length -lt 4 -or $d[1] -ne 1) { if ($d.Length -ge 2 -and $d[1] -eq 2) { Send-Error 2 "read only" $ep }; continue }
    $fields = [System.Text.Encoding]::ASCII.GetString($d, 2, $d.Length - 2).Split([char]0)
    $name = $fields[0].Replace('/', '\').TrimStart('\')
    $opts = @{}
    for ($i = 2; $i + 1 -lt $fields.Length; $i += 2) { if ($fields[$i]) { $opts[$fields[$i].ToLower()] = $fields[$i + 1] } }
    $full = [System.IO.Path]::GetFullPath((Join-Path $Root $name))
    if (-not $full.StartsWith([System.IO.Path]::GetFullPath($Root)) -or -not (Test-Path -LiteralPath $full -PathType Leaf)) {
        Write-Host "tftpd: $($ep.Address) asked for missing $name"
        Send-Error 1 "file not found" $ep
        continue
    }
    $data = [System.IO.File]::ReadAllBytes($full)
    $blksize = 512
    $oack = New-Object System.Collections.Generic.List[byte]
    $oack.AddRange([byte[]](0, 6))
    $append = { param($k, $v) $oack.AddRange([System.Text.Encoding]::ASCII.GetBytes("$k")); $oack.Add(0); $oack.AddRange([System.Text.Encoding]::ASCII.GetBytes("$v")); $oack.Add(0) }
    if ($opts.ContainsKey('blksize')) { $blksize = [Math]::Min([Math]::Max([int]$opts['blksize'], 8), 1468); & $append 'blksize' $blksize }
    if ($opts.ContainsKey('tsize')) { & $append 'tsize' $data.Length }
    if ($opts.ContainsKey('timeout')) { & $append 'timeout' $opts['timeout'] }
    $start = Get-Date
    Write-Host "tftpd: $($ep.Address) <- $name ($($data.Length) bytes, blksize $blksize)"
    if ($oack.Count -gt 2) {
        if (-not (Send-Reliable $oack.ToArray() 0 $ep)) { Write-Host "tftpd: no ACK for options"; continue }
    }
    $block = 1; $offset = 0; $ok = $true
    while ($true) {
        $n = [Math]::Min($blksize, $data.Length - $offset)
        $p = New-Object byte[] (4 + $n)
        $p[1] = 3; $p[2] = [byte](($block -shr 8) -band 0xFF); $p[3] = [byte]($block -band 0xFF)
        if ($n -gt 0) { [Array]::Copy($data, $offset, $p, 4, $n) }
        if (-not (Send-Reliable $p $block $ep)) { $ok = $false; break }
        $offset += $n; $block++
        if ($n -lt $blksize) { break }
    }
    $secs = ((Get-Date) - $start).TotalSeconds
    if ($ok) { Write-Host ("tftpd: sent {0} in {1:N1} s" -f $name, $secs) } else { Write-Host "tftpd: transfer of $name failed" }
}
