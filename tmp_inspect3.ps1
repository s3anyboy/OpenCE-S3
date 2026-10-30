param([string]$Path)
$s = [System.IO.File]::OpenRead($Path)
$hdr = New-Object byte[] 2048
[void]$s.Read($hdr, 0, 2048)
$tagOff = [BitConverter]::ToInt32($hdr, 0x10)
$tagSize = [BitConverter]::ToInt32($hdr, 0x14)
Write-Output ("$Path tdoff=$tagOff tdsize=$tagSize")
$s.Position = $tagOff
$th = New-Object byte[] $tagSize
$read = 0
while ($read -lt $tagSize) { $n = $s.Read($th, $read, $tagSize - $read); if ($n -le 0) { break }; $read += $n }
$s.Close()
$inst = [BitConverter]::ToUInt32($th, 0)
$count = [BitConverter]::ToInt32($th, 0x0C)
$b = [int64]0x8069E000
$io = $inst - $b
Write-Output ("  instances=0x{0:X8} count={1} offset {2} stride 0x24" -f $inst,$count,$io)
$list = New-Object System.Collections.ArrayList
for ($i = 0; $i -lt $count; $i++) {
  $o = $io + $i * 0x24
  if ($o + 0x24 -gt $th.Length) { break }
  $g = [System.Text.Encoding]::ASCII.GetString([byte[]]@($th[$o+3],$th[$o+2],$th[$o+1],$th[$o]), 0, 4)
  # name pointer: look at 0x10 and 0x14
  $name = ''
  foreach ($no in @(0x10, 0x18)) {
    $nameAddr = [BitConverter]::ToUInt32($th, $o + $no)
    if ($nameAddr -ge $b -and ($nameAddr - $b) -lt $th.Length) {
      $p = $nameAddr - $b
      $e = $p
      while ($e -lt $th.Length -and $th[$e] -ge 32 -and $th[$e] -lt 127 -and ($e - $p) -lt 256) { $e++ }
      if ($e - $p -gt 2) { $name = [System.Text.Encoding]::ASCII.GetString($th, $p, $e - $p); break }
    }
  }
  $addr = [BitConverter]::ToUInt32($th, $o + 0x14)
  [void]$list.Add([pscustomobject]@{ Index=$i; Group=$g; Address=$addr; Name=$name })
}
$list | Group-Object Group | Sort-Object Name | ForEach-Object { Write-Output ("  group {0} x{1}" -f $_.Name, $_.Count) }
Write-Output '--- Shadowrun groups:'
$list | Where-Object { 'srud','srac','srwd','buym','m1ed','m2ed','m3ed','t1ed','t2ed','t3ed' -contains $_.Group } | ForEach-Object { Write-Output ("  [{0}] {1} @0x{2:X8} {3}" -f $_.Index,$_.Group,$_.Address,$_.Name) }
Write-Output '--- bipds:'
$list | Where-Object { $_.Group -eq 'bipd' } | ForEach-Object { Write-Output ("  [{0}] {1} @0x{2:X8} {3}" -f $_.Index,$_.Group,$_.Address,$_.Name) }
