param(
  [string]$Tag,
  [int]$Runs = 20,
  [int]$Jobs = 1,
  [string]$Plan = "test\plans\feat\compile\ref.plan.toml",
  [int]$Load = 0,
  [ValidateSet('case','opt')][string]$LoadKind = 'case',
  [string]$Workdir = "D:\Projects\Camel-wt\countdet"
)

$ErrorActionPreference = 'Continue'
$outDir = Join-Path $env:TEMP "countdet"
$logDir = Join-Path $outDir "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$tsv = Join-Path $outDir ("{0}.tsv" -f $Tag)
"run`tstart`tsecs`tload_before`tload_after`tpass`tfail`tten_fail_backends`tcount_lines" | Set-Content -Encoding utf8 $tsv

$env:CAMEL_HOME = Join-Path $Workdir "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"

$loadProcs = @()
if ($Load -gt 0) {
  $loadLog = Join-Path $logDir ("{0}-load.log" -f $Tag)
  $case = Join-Path $Workdir "test\cases\compile\ref\ref_boxes_made_per_call_ten.cml"
  $inner = if ($LoadKind -eq 'case') {
    "`$n=0; while (`$n -lt 100000) { & camel --gc-stress 'alloc=7,safepoint=3,kind=both' --gc-check 'foreign,verify=both' --gc generational '$case' std::prl *>> '$loadLog'; `$n++ }"
  } else {
    "`$n=0; while (`$n -lt 100000) { & camel '$Workdir\test\cases\perf\fib_recursive.cml' std::opt *>> '$loadLog'; `$n++ }"
  }
  for ($i = 1; $i -le $Load; $i++) {
    $loadProcs += Start-Process -FilePath "powershell" -PassThru -WindowStyle Hidden -ArgumentList @(
      "-NoProfile", "-Command", "`$env:CAMEL_HOME='$($env:CAMEL_HOME)'; `$env:PATH=`"`$env:CAMEL_HOME\bin;`$env:PATH`"; $inner"
    )
  }
  Write-Host "load workers: $Load ($LoadKind)"
  Start-Sleep -Seconds 3
}

function Get-LoadPct {
  try { [math]::Round((Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 1).CounterSamples.CookedValue, 1) } catch { -1 }
}

try {
  for ($r = 1; $r -le $Runs; $r++) {
    $loadBefore = Get-LoadPct
    $start = Get-Date -Format 'HH:mm:ss'
    $log = Join-Path $logDir ("{0}-{1:d2}.log" -f $Tag, $r)
    Push-Location $Workdir
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $text = & node scripts/test.js $Plan --jobs $Jobs 2>&1 | Out-String
    $sw.Stop()
    Pop-Location
    $text | Set-Content -Encoding utf8 $log
    $loadAfter = Get-LoadPct

    $plain = [regex]::Replace($text, '\x1b\[[0-9;]*[A-Za-z]', '')
    $m = [regex]::Matches($plain, '\[(PASS|FAIL|XFAIL|SKIP)\]\s+(\S+)')
    $passes = ($m | Where-Object { $_.Groups[1].Value -eq 'PASS' }).Count
    $fails = ($m | Where-Object { $_.Groups[1].Value -eq 'FAIL' }).Count
    $tenFail = ($m | Where-Object {
        $_.Groups[1].Value -eq 'FAIL' -and ($_.Groups[2].Value -like 'ref.boxes_made_per_call_ten.*' -or $_.Groups[2].Value -eq 'ref.boxes_made_per_call_ten')
      } | ForEach-Object { $_.Groups[2].Value }) -join ','
    $counts = ([regex]::Matches($plain, 'FOREIGN_RESOURCES[^\r\n]*') | ForEach-Object { ($_.Value -replace '\s+', ' ') }) -join ' || '
    $row = @($r, $start, [math]::Round($sw.Elapsed.TotalSeconds, 1), $loadBefore, $loadAfter, $passes, $fails, $tenFail, $counts) -join "`t"
    Add-Content -Encoding utf8 -Path $tsv -Value $row
    Write-Host $row
    if ($text -match '\[FAIL\]\s+ref\.boxes_made_per_call_(ten|thousand)') {
      ($text -split "`r?`n" | Where-Object { $_ -match '\[FAIL\]|FOREIGN_RESOURCES|assert|nothing accumulated|as many as' } | Select-Object -First 8) | ForEach-Object { Write-Host "    > $_" }
    }
  }
} finally {
  foreach ($p in $loadProcs) { try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {} }
}
Write-Host "done: $tsv"
