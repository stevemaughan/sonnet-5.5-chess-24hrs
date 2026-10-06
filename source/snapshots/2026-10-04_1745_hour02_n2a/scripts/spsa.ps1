# SPSA tuner driving fastchess.  usage: spsa.ps1 -Exe engine.exe -Iters 300 -Rounds 8 -TC 5+0.05 -ParamFile params.txt -Out spsa_state.txt
# params.txt lines:  Name,init,min,max,c   (c = perturbation size at iteration 1)
param([string]$Exe, [int]$Iters = 300, [int]$Rounds = 8, [string]$TC = "5+0.05", [string]$ParamFile, [string]$Out = "source\tests\spsa_state.txt",
      [string]$ExtraOpts = "", [int]$Conc = 10, [double]$A = 0.0, [double]$Alpha = 0.602, [double]$Gamma = 0.101, [double]$R = 0.02)
$root = "C:\Users\steve\Dropbox\Chess\Development\AI Benchmarks\Sonnet 5.5"
Set-Location $root
$params = @()
foreach ($l in Get-Content $ParamFile) {
    if ($l.Trim() -eq "" -or $l.StartsWith("#")) { continue }
    $p = $l.Split(",")
    $params += [pscustomobject]@{ Name = $p[0]; Val = [double]$p[1]; Min = [double]$p[2]; Max = [double]$p[3]; C = [double]$p[4] }
}
if ($A -le 0) { $A = $Iters / 10.0 }
$rnd = New-Object System.Random
for ($k = 1; $k -le $Iters; $k++) {
    $ck = [math]::Pow($k, -$Gamma)
    $ak = $R / [math]::Pow($k + $A, $Alpha)   # step multiplier (in units of c per unit score)
    $delta = @(); $optsP = @(); $optsM = @()
    foreach ($p in $params) {
        $d = if ($rnd.Next(2) -eq 0) { -1 } else { 1 }
        $delta += $d
        $pp = [math]::Min($p.Max, [math]::Max($p.Min, $p.Val + $d * $p.C * $ck))
        $pm = [math]::Min($p.Max, [math]::Max($p.Min, $p.Val - $d * $p.C * $ck))
        $optsP += "option.$($p.Name)=$([int][math]::Round($pp))"
        $optsM += "option.$($p.Name)=$([int][math]::Round($pm))"
    }
    $args2 = @("-engine", "cmd=$Exe", "name=P", "option.Hash=16") + $optsP + @("-engine", "cmd=$Exe", "name=M", "option.Hash=16") + $optsM +
        @("-each", "tc=$TC") + ($ExtraOpts -split " " | Where-Object { $_ }) +
        @("-openings", "file=resources\fastchess\UHO.pgn", "format=pgn", "order=random", "plies=16", "-rounds", "$Rounds", "-repeat",
          "-concurrency", "$Conc", "-recover")
    $res = & .\resources\fastchess\fastchess.exe @args2 2>&1 | Select-String -Pattern "^Games:" | Select-Object -Last 1
    if (-not $res) { Write-Host "iter $k: no result"; continue }
    if ($res.Line -match "Games: (\d+), Wins: (\d+), Losses: (\d+), Draws: (\d+)") {
        $n = [double]$Matches[1]; $w = [double]$Matches[2]; $lo = [double]$Matches[3]
        $s = ($w - $lo) / $n    # in [-1,1]
    } else { continue }
    for ($i = 0; $i -lt $params.Count; $i++) {
        $p = $params[$i]
        $p.Val = [math]::Min($p.Max, [math]::Max($p.Min, $p.Val + $ak * $s * $delta[$i] * $p.C / $ck * 4))
    }
    $line = "iter $k score $([math]::Round($s,3)) :: " + (($params | ForEach-Object { "$($_.Name)=$([math]::Round($_.Val,1))" }) -join " ")
    Write-Host $line
    Add-Content $Out $line
}
