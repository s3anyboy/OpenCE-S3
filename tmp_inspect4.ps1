param([string]$Path, [int]$Stride = 0x20, [int]$First = 0)
$fs = [System.IO.File]::OpenRead($Path)
$hdr = New-Object byte[] 2048
[void]$fs.Read($hdr, 0, 2048)
$tagOff = [BitConverter]::ToInt32($hdr, 0x10)
$tagSize = [BitConverter]::ToInt32($hdr, 0x14)
$fs.Position = $tagOff
$s = New-Object System.IO.MemoryStream
$buf = New-Object byte[] (1MB)
$left = $tagSize
while ($left -gt 0) { $n = $fs.Read($buf, 0, [Math]::Min(1MB, $left)); if ($n -le 0) { break }; $s.Write($buf, 0, $n); $left -= $n }
$fs.Close()
$th = $s.ToArray()
$b = [int64]0x8069E000
if (($th[0x24..0x27] | ForEach-Object { $_.ToString('X2') }) -join '' -ne '73636E72') { $b = [int64]0x806D0000 }

Write-Output ('  tag data base 0x{0:X8}' -f $b)
$inst = [BitConverter]::ToUInt32($th, 0)
$count = [BitConverter]::ToInt32($th, 0x0C)
$io = $inst - $b
Write-Output ("$Path tdsize=$tagSize instances=0x{0:X8} count={1} offset={2} stride=0x{3:X} first={4}" -f $inst, $count, $io, $Stride, $First)
function Get-Name([int64]$addr) {
  if ($addr -lt $b) { return '' }
  $p = $addr - $b
  if ($p -ge $th.Length) { return '' }
  $e = $p
  while ($e -lt $th.Length -and $th[$e] -ge 32 -and $th[$e] -lt 127 -and ($e - $p) -lt 200) { $e++ }
  return [System.Text.Encoding]::ASCII.GetString($th, $p, $e - $p)
}
$list = New-Object System.Collections.ArrayList
for ($i = 0; $i -lt $count; $i++) {
  $o = $io + $First + $i * $Stride
  if ($o + $Stride -gt $th.Length) { break }
  if ($Stride -eq 0x24) {
    $g = [System.Text.Encoding]::ASCII.GetString([byte[]]@($th[$o + 3], $th[$o + 2], $th[$o + 1], $th[$o]), 0, 4)
    $name = Get-Name ([BitConverter]::ToUInt32($th, $o + 0x10))
    if (-not $name) { $name = Get-Name ([BitConverter]::ToUInt32($th, $o + 0x18)) }
    $addr = [BitConverter]::ToUInt32($th, $o + 0x14)
  }
  else {
    $g = [System.Text.Encoding]::ASCII.GetString([byte[]]@($th[$o + 3], $th[$o + 2], $th[$o + 1], $th[$o]), 0, 4)
    $name = Get-Name ([BitConverter]::ToUInt32($th, $o + 0x10))
    $addr = [BitConverter]::ToUInt32($th, $o + 0x14)
  }
  [void]$list.Add([pscustomobject]@{ Index = $i; Group = $g; Address = $addr; Name = $name })
}
$list | Where-Object { $_.Group -match '^[0-9a-zA-Z !]{4}$' } | Group-Object Group | Sort-Object Name | ForEach-Object { Write-Output ("  group {0} x{1}" -f $_.Name, $_.Count) }
Write-Output '--- Shadowrun groups:'
$list | Where-Object { 'srud', 'srac', 'srwd', 'buym', 'm1ed', 'm2ed', 'm3ed', 't1ed', 't2ed', 't3ed', 'matg', 'scnr' -contains $_.Group } | ForEach-Object { Write-Output ("  [{0}] {1} @0x{2:X8} {3}" -f $_.Index, $_.Group, $_.Address, $_.Name) }
Write-Output '--- bipds:'
$list | Where-Object { $_.Group -eq 'bipd' } | ForEach-Object { Write-Output ("  [{0}] {1} @0x{2:X8} {3}" -f $_.Index, $_.Group, $_.Address, $_.Name) }
