param(
  [string]$Tag,
  [int]$Runs = 20,
  [int]$Jobs = 1,
  [string]$Plan = "test\plans\feat\compile\ref.plan.toml",
  [string]$Workdir = "D:\Projects\Camel-wt\prlcount"
)

# One row per run: the counts the run read, and the [PASS]/[FAIL] lines of the counting entries.
# Taken from the harness of the same name in
# docs/handover/reports/2026-11-15-counted-boxes-under-load/ (lane-countdet), with the load arms
# and the CPU probes dropped: this lane needs the serial 20-run reading only.
$ErrorActionPreference = 'Continue'
$outDir = Join-Path $env:TEMP "prlcount"
$logDir = Join-Path $outDir "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$tsv = Join-Path $outDir ("{0}.tsv" -f $Tag)
"run`tstart`tsecs`tpass`tfail`tten_red`tcount_lines" | Set-Content -Encoding utf8 $tsv

$env:CAMEL_HOME = Join-Path $Workdir "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"

for ($r = 1; $r -le $Runs; $r++) {
  $start = Get-Date -Format 'HH:mm:ss'
  $log = Join-Path $logDir ("{0}-{1:d2}.log" -f $Tag, $r)
  Push-Location $Workdir
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $text = & node scripts/test.js $Plan --jobs $Jobs 2>&1 | Out-String
  $sw.Stop()
  Pop-Location
  $text | Set-Content -Encoding utf8 $log

  $plain = [regex]::Replace($text, '\x1b\[[0-9;]*[A-Za-z]', '')
  $m = [regex]::Matches($plain, '\[(PASS|FAIL|XFAIL|SKIP)\]\s+(\S+)')
  $passes = ($m | Where-Object { $_.Groups[1].Value -eq 'PASS' }).Count
  $fails = ($m | Where-Object { $_.Groups[1].Value -eq 'FAIL' }).Count
  $tenRed = ($m | Where-Object {
      $_.Groups[1].Value -eq 'FAIL' -and ($_.Groups[2].Value -like 'ref.boxes_made_per_call_*')
    } | ForEach-Object { $_.Groups[2].Value }) -join ','
  $counts = ([regex]::Matches($plain, 'FOREIGN_RESOURCES[^\r\n]*') | ForEach-Object {
      ($_.Value -replace '\s+', ' ') }) -join ' || '
  $row = @($r, $start, [math]::Round($sw.Elapsed.TotalSeconds, 1), $passes, $fails, $tenRed, $counts) -join "`t"
  Add-Content -Encoding utf8 -Path $tsv -Value $row
  Write-Host $row
}
Write-Host "done: $tsv"