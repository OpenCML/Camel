param(
  [string]$Tag,
  [string]$Backend = "std::prl",
  [int]$Rounds = 20,
  [int]$Width = 1,
  [string]$Workdir = "D:\Projects\Camel-wt\countdet"
)

$ErrorActionPreference = 'Continue'
$outDir = Join-Path $env:TEMP "countdet"
$caseLog = Join-Path $outDir "case-logs"
New-Item -ItemType Directory -Force -Path $caseLog | Out-Null
$tsv = Join-Path $outDir ("{0}.tsv" -f $Tag)
"round`twall_s`tload_before`tload_after`treadings(created/released/live/disposed;exit)" | Set-Content -Encoding utf8 $tsv

$env:CAMEL_HOME = Join-Path $Workdir "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
$case = Join-Path $Workdir "test\cases\compile\ref\ref_boxes_made_per_call_ten.cml"

function Get-LoadPct {
  try { [math]::Round((Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 1).CounterSamples.CookedValue, 1) } catch { -1 }
}

for ($r = 1; $r -le $Rounds; $r++) {
  $loadBefore = Get-LoadPct
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $procs = @()
  $outs = @()
  for ($i = 1; $i -le $Width; $i++) {
    $o = Join-Path $caseLog ("{0}-r{1:d2}-w{2}.out" -f $Tag, $r, $i)
    $e = Join-Path $caseLog ("{0}-r{1:d2}-w{2}.err" -f $Tag, $r, $i)
    $outs += $o
    $procs += Start-Process -FilePath (Join-Path $env:CAMEL_HOME "bin\camel.exe") -PassThru -WindowStyle Hidden `
      -RedirectStandardOutput $o -RedirectStandardError $e `
      -ArgumentList @("--gc-stress", "alloc=7,safepoint=3,kind=both", "--gc-check", "foreign,verify=both", "--gc", "generational", $case, $Backend)
  }
  $procs | ForEach-Object { $_.WaitForExit() }
  $sw.Stop()
  $loadAfter = Get-LoadPct
  $readings = foreach ($p in $procs) {
    $txt = (Get-Content -Raw -ErrorAction SilentlyContinue $outs[$procs.IndexOf($p)])
    $line = [regex]::Match([string]$txt, 'FOREIGN_RESOURCES[^\r\n]*').Value
    if ($line) { $c = @('created','released','live','disposed') | ForEach-Object { ([regex]::Match($line, "$_=(\d+)").Groups[1].Value) }; ($c -join '/') + ';exit=' + $p.ExitCode }
    else { 'no-count-line;exit=' + $p.ExitCode }
  }
  $row = @($r, [math]::Round($sw.Elapsed.TotalSeconds, 2), $loadBefore, $loadAfter, ($readings -join ' | ')) -join "`t"
  Add-Content -Encoding utf8 -Path $tsv -Value $row
  Write-Host $row
}
Write-Host "done: $tsv"