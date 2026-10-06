# usage: match.ps1 -A new.exe -B old.exe -Rounds 100 -TC 10+0.1 -Tag name [-Conc 10] [-Sprt] [-AOpts "option.X=1"] [-BOpts ...]
param([string]$A, [string]$B, [int]$Rounds = 100, [string]$TC = "10+0.1", [string]$Tag = "m", [int]$Conc = 10,
      [switch]$Sprt, [string]$AOpts = "", [string]$BOpts = "", [double]$Elo0 = 0, [double]$Elo1 = 5)
$root = "C:\Users\steve\Dropbox\Chess\Development\AI Benchmarks\Sonnet 5.5"
Set-Location $root
$sprtArgs = @()
if ($Sprt) { $sprtArgs = @("-sprt", "elo0=$Elo0", "elo1=$Elo1", "alpha=0.05", "beta=0.05") }
$engA = @("-engine", "cmd=$A", "name=A", "option.Hash=256") + ($AOpts -split " " | Where-Object { $_ })
$engB = @("-engine", "cmd=$B", "name=B", "option.Hash=256") + ($BOpts -split " " | Where-Object { $_ })
$args2 = $engA + $engB + @("-each", "tc=$TC", "-openings", "file=resources\fastchess\UHO.pgn", "format=pgn", "order=random", "plies=16",
    "-rounds", "$Rounds", "-repeat", "-concurrency", "$Conc", "-recover", "-ratinginterval", "50", "-pgnout", "file=source\tests\$Tag.pgn") + $sprtArgs
& .\resources\fastchess\fastchess.exe @args2 2>&1 | Tee-Object -FilePath "source\tests\$Tag.log" | Select-String -Pattern "Elo:|Games:|Ptnml|LLR|SPRT|Finished match|loses|timeout|illegal|crash|disconnect" | ForEach-Object { $_.Line }
