param([string]$Path, [string]$Base)
$s = [System.IO.File]::OpenRead($Path)
$hdr = New-Object byte[] 2048
[void]$s.Read($hdr, 0, 2048)
$enc = [System.Text.Encoding]::ASCII
$tagOff = [BitConverter]::ToInt32($hdr, 0x10)
$tagSize = [BitConverter]::ToInt32($hdr, 0x14)
Write-Output ("$Path name=" + $enc.GetString($hdr, 0x20, 32).TrimEnd([char]0) + " build=" + $enc.GetString($hdr, 0x40, 32).TrimEnd([char]0) + " tdoff=$tagOff tdsize=$tagSize")
$s.Position = $tagOff
$th = New-Object byte[] $tagSize
$read = 0
while ($read -lt $tagSize) { $n = $s.Read($th, $read, $tagSize - $read); if ($n -le 0) { break }; $read += $n }
$s.Close()
$inst = [BitConverter]::ToUInt32($th, 0)
$count = [BitConverter]::ToInt32($th, 0x0C)
$b = [int64]$Base
$io = $inst - $b
Write-Output ("  instances=0x{0:X8} count={1} using base 0x{2:X8} offset {3}" -f $inst, $count, $b, $io)
if ($io -lt 0 -or $io -ge $th.Length) { Write-Output 'bad base'; return }
$list = New-Object System.Collections.ArrayList
# the first 4 entries describe the layout; pick the stride that yields known groups
$stride = 0x20; $first = 0
foreach ($tryStride in @(0x20, 0x1C, 0x18, 0x24)) {
  foreach ($tryFirst in @(0, 4)) {
    $ok = 0
    foreach ($k in 3..9) {
      $oo = $io + $tryFirst + $k * $tryStride
      if ($oo + 4 -le $th.Length) {
        $gg = [System.Text.Encoding]::ASCII.GetString([byte[]]@($th[$oo+3],$th[$oo+2],$th[$oo+1],$th[$oo]), 0, 4)
        if ($gg -match '^[0-9a-zA-Z !]{4}$') { $ok++ }
      }
    }
    if ($ok -ge 5) { $stride = $tryStride; $first = $tryFirst }
  }
}
Write-Output ('  stride 0x{0:X} first {1}' -f $stride, $first)
for ($i = 0; $i -lt $count; $i++) {
  $o = $io + $first + $i * $stride
  if ($o + $stride -gt $th.Length) { break }
  $g = [System.Text.Encoding]::ASCII.GetString([byte[]]@($th[$o+3],$th[$o+2],$th[$o+1],$th[$o]), 0, 4)
  $nameAddr = [BitConverter]::ToUInt32($th, $o + 0x10)
  $addr = [BitConverter]::ToUInt32($th, $o + 0x14)
  $name = ''
  if ($nameAddr -ge $b -and ($nameAddr - $b) -lt $th.Length) {
    $p = $nameAddr - $b
    $e = $p
    while ($e -lt $th.Length -and $th[$e] -ne 0 -and ($e - $p) -lt 256) { $e++ }
    $name = $enc.GetString($th, $p, $e - $p)
  }
  [void]$list.Add([pscustomobject]@{ Index = $i; Group = $g; Address = $addr; Name = $name })
}
$list | Where-Object { $_.Group -match '^[0-9a-zA-Z !]{4}$' } | Group-Object Group | Sort-Object Name | ForEach-Object { Write-Output ("  group {0} x{1}" -f $_.Name, $_.Count) }
Write-Output '--- srud/srac/srwd/buym/m-ed/glob/bipd tags:'
$list | Where-Object { 'srud', 'srac', 'srwd', 'buym', 'm1ed', 'm2ed', 'm3ed', 't1ed', 't2ed', 't3ed' -contains $_.Group } | ForEach-Object { Write-Output ("  [{0}] {1} @0x{2:X8} {3}" -f $_.Index, $_.Group, $_.Address, $_.Name) }
Write-Output '--- bipds:'
$list | Where-Object { $_.Group -eq 'bipd' } | ForEach-Object { Write-Output ("  [{0}] {1} @0x{2:X8} {3}" -f $_.Index, $_.Group, $_.Address, $_.Name) }
