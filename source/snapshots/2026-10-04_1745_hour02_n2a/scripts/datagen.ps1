# usage: datagen.ps1 -Exe path -Procs 9 -Minutes 40 -Nodes 1500 -Tag gen1 -SeedBase 100 [-Book path] [-BookPct 50] [-Net path]
param([string]$Exe, [int]$Procs = 9, [double]$Minutes = 40, [int]$Nodes = 1500, [string]$Tag = "gen1", [int]$SeedBase = 100,
      [int]$RandPlies = 8, [string]$Book = "none", [int]$BookPct = 0, [string]$Net = "none")
$dir = "C:\Users\steve\chess_nnue_data\$Tag"
New-Item -ItemType Directory -Force $dir | Out-Null
for ($i = 0; $i -lt $Procs; $i++) {
    $seed = $SeedBase + $i
    $a = "datagen `"$dir\d$seed.bin`" $Minutes $Nodes $seed $RandPlies `"$Book`" $BookPct `"$Net`""
    Start-Process -FilePath $Exe -ArgumentList $a -WindowStyle Hidden -RedirectStandardError "$dir\log$seed.txt"
}
"launched $Procs procs -> $dir"
