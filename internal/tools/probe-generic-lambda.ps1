# Probe driver: whether a lambda whose parameter type is not written takes its type from the
# position it stands in (design principles 5.1, "the expected type travels into a call along the
# call's type variables"), measured against a plain generic function -- not an operator, whose
# declaration is also its export (that was the broken control earlier: removing `#op` from `map`
# removed `map` itself).
# Prints the SHA-256 of every probe program it runs before running any of them, so a reading is
# tied to the exact sources; the programs live outside the repository, under -Root.
param(
    [string]$Root = "D:\Temp\bench7",
    [string]$CamelHome = "D:\Projects\Camel-wt\backend-speed\out\latest"
)
$ErrorActionPreference = "Stop"
$env:CAMEL_HOME = $CamelHome
$env:PATH = "$CamelHome\bin;$env:PATH"
$camel = Join-Path $CamelHome "bin\camel.exe"
$names = @("u", "t2", "u2", "wu", "wt")
foreach ($name in $names) {
    $file = Join-Path $Root "$name\main.cml"
    Write-Output ("probe {0} sha256-16 {1}" -f $name,
        (Get-FileHash -Algorithm SHA256 -Path $file).Hash.Substring(0, 16))
}
$load = (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
Write-Output ("load {0}%" -f [math]::Round($load, 0))
foreach ($name in $names) {
    $file = Join-Path $Root "$name\main.cml"
    $lines = @(& $camel $file std::fvm 2>&1)
    $sha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash(
        [Text.Encoding]::UTF8.GetBytes(($lines -join "`n")))).Replace("-", "").Substring(0, 16)
    Write-Output ("--- {0}: out sha256-16 {1}" -f $name, $sha)
    $lines | Select-Object -First 6 | ForEach-Object { Write-Output $_.ToString() }
}
