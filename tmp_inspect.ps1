param([string]$Path)
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
# the tag data's base is its tag header's 0x24-byte own address: the file offset is its address less the base
$inst = [BitConverter]::ToUInt32($th, 0)
$b = [int64]$inst - 0x24
$count = [BitConverter]::ToInt32($th, 0x0C)
Write-Output ("$Path tdsize=$tagSize base=0x{0:X8} count={1}" -f $b, $count)
$io = 0x24
$known = 'actr','actv','antr','bipd','bitm','buym','coll','colo','cont','deca','DeLa','effe','elec','eqip','font','foot','garb','grhi','hmt ','hudg','itmc','jpt!','lens','ligh','lsnd','m1ed','m2ed','m3ed','mach','matg','metr','mgs2','mode','part','pctl','phys','pphy','proj','sbsp','scen','schi','scnr','senv','sgla','sky ','snd!','snde','soso','sotr','Soul','spla','srac','srud','srwd','ssce','t1ed','t2ed','t3ed','trak','udlg','unhi','ustr','vehi','weap','wind','wphi'
function Get-Name([int64]$addr) {
  if ($addr -lt $b) { return '' }
  $p = $addr - $b
  if ($p -lt 0 -or $p -ge $th.Length) { return '' }
  $e = $p
  while ($e -lt $th.Length -and $th[$e] -ge 32 -and $th[$e] -lt 127 -and ($e - $p) -lt 200) { $e++ }
  return [System.Text.Encoding]::ASCII.GetString($th, $p, $e - $p)
}
$list = New-Object System.Collections.ArrayList
for ($i = 0; $i -lt $count; $i++) {
  $o = $io + $i * 0x24
  if ($o + 0x24 -gt $th.Length) { break }
  $g = [System.Text.Encoding]::ASCII.GetString([byte[]]@($th[$o+3],$th[$o+2],$th[$o+1],$th[$o]), 0, 4)
  $name = Get-Name ([BitConverter]::ToUInt32($th, $o + 0x10))
  if (-not $name) { $name = Get-Name ([BitConverter]::ToUInt32($th, $o + 0x18)) }
  $addr = [BitConverter]::ToUInt32($th, $o + 0x14)
  [void]$list.Add([pscustomobject]@{ Index=$i; Group=$g; Address=$addr; Name=$name })
}
$list | Where-Object { $known -contains $_.Group } | Group-Object Group | Sort-Object Name | ForEach-Object { Write-Output ("  group {0} x{1}" -f $_.Name, $_.Count) }
Write-Output '--- other groups:'
$list | Where-Object { $known -notcontains $_.Group } | Group-Object Group | Sort-Object Name | ForEach-Object { Write-Output ("  group {0} x{1}" -f $_.Name, $_.Count) }
Write-Output '--- Shadowrun groups:'
$list | Where-Object { 'srud','srac','srwd','buym','m1ed','m2ed','m3ed','t1ed','t2ed','t3ed','matg','scnr' -contains $_.Group } | ForEach-Object { Write-Output ("  [{0}] {1} @0x{2:X8} {3}" -f $_.Index,$_.Group,$_.Address,$_.Name) }
Write-Output '--- bipds:'
$list | Where-Object { $_.Group -eq 'bipd' } | ForEach-Object { Write-Output ("  [{0}] {1} @0x{2:X8} {3}" -f $_.Index,$_.Group,$_.Address,$_.Name) }
