param(
  [string]$Tag,
  [string]$Backend = "std::prl",
  [int]$Rounds = 20,
  [int]$Width = 1,
  [string]$Case = "test\cases\compile\ref\ref_boxes_made_per_call_ten.cml",
  [switch]$NoGcStress,
  [string]$PassParams = "",
  [string]$Workdir = "D:\Projects\Camel-wt\prlcount"
)

# One row per round: created/released/live/disposed and the exit code, exactly as the CLI printed
# them. Taken from the harness of the same name in
# docs/handover/reports/2026-11-15-counted-boxes-under-load/ (lane-countdet).
$ErrorActionPreference = 'Continue'
$outDir = Join-Path $env:TEMP "prlcount"
$caseLog = Join-Path $outDir "case-logs"
New-Item -ItemType Directory -Force -Path $caseLog | Out-Null
$tsv = Join-Path $outDir ("{0}.tsv" -f $Tag)
"round`twall_s`treadings(created/released/live/disposed;exit)" | Set-Content -Encoding utf8 $tsv

$env:CAMEL_HOME = Join-Path $Workdir "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
$fullCase = Join-Path $Workdir $Case

$argsBase = @()
if (-not $NoGcStress) { $argsBase += @("--gc-stress", "alloc=7,safepoint=3,kind=both") }
$argsBase += @("--gc-check", "foreign,verify=both", "--gc", "generational", $fullCase, $Backend)
$extra = @()
if ($PassParams -ne "") { $extra = $PassParams.Split(' ') }

for ($r = 1; $r -le $Rounds; $r++) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $procs = @()
  $outs = @()
  for ($i = 1; $i -le $Width; $i++) {
    $o = Join-Path $caseLog ("{0}-r{1:d2}-w{2}.out" -f $Tag, $r, $i)
    $e = Join-Path $caseLog ("{0}-r{1:d2}-w{2}.err" -f $Tag, $r, $i)
    $outs += $o
    $procs += Start-Process -FilePath (Join-Path $env:CAMEL_HOME "bin\camel.exe") -PassThru -WindowStyle Hidden `
      -RedirectStandardOutput $o -RedirectStandardError $e `
      -ArgumentList ($argsBase + $extra)
  }
  $procs | ForEach-Object { $_.WaitForExit(); $null = $_.Handle }
  $sw.Stop()
  $readings = for ($k = 0; $k -lt $procs.Count; $k++) {
    $txt = [string](Get-Content -Raw -ErrorAction SilentlyContinue $outs[$k])
    $line = [regex]::Match($txt, 'FOREIGN_RESOURCES[^\r\n]*').Value
    $exit = $procs[$k].ExitCode
    if ($line) {
      $c = @('created', 'released', 'live', 'disposed') | ForEach-Object { ([regex]::Match($line, "$_=(\d+)").Groups[1].Value) }
      ($c -join '/') + ';exit=' + $exit
    } else { 'no-count-line;exit=' + $exit }
  }
  $row = @($r, [math]::Round($sw.Elapsed.TotalSeconds, 2), ($readings -join ' | ')) -join "`t"
  Add-Content -Encoding utf8 -Path $tsv -Value $row
  Write-Host $row
}
Write-Host "done: $tsv"