# Probe driver: whether `map` written with a body (a collecting loop in the standard library) can
# take a lambda whose parameter type is not written, and the same lambda with the type written.
# The first lines it prints are the SHA-256 of the probe programs it runs, so a reading can be
# tied to the exact sources; the probe programs themselves stay outside the repo.
param(
    [string]$Root = "D:\Temp\bench6",
    [string]$CamelHome = "D:\Projects\Camel-wt\backend-speed\out\latest"
)
$ErrorActionPreference = "Stop"
$env:CAMEL_HOME = $CamelHome
$env:PATH = "$CamelHome\bin;$env:PATH"
$camel = Join-Path $CamelHome "bin\camel.exe"
foreach ($name in @("untyped", "typed", "comp")) {
    $file = Join-Path $Root "$name\main.cml"
    $sha = (Get-FileHash -Algorithm SHA256 -Path $file).Hash.Substring(0, 16)
    Write-Output ("probe {0} sha256-16 {1}" -f $name, $sha)
}
$load = (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
Write-Output ("load {0}%" -f [math]::Round($load, 0))
foreach ($name in @("untyped", "typed", "comp")) {
    $file = Join-Path $Root "$name\main.cml"
    $out = & $camel $file std::fvm 2>&1
    $lines = @($out)
    $first = if ($lines.Count -gt 0) { $lines[0].ToString() } else { "" }
    $sha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash(
        [Text.Encoding]::UTF8.GetBytes(($lines -join "`n")))).Replace("-", "").Substring(0, 16)
    Write-Output ("--- {0}: {1}" -f $name, $sha)
    $lines | Select-Object -First 6 | ForEach-Object { Write-Output $_.ToString() }
}
