#!/usr/bin/env pwsh
# Instrument, not a test: writes A-libraries-files.csv, the per-file list behind A-libraries.csv.
# lines is `wc -l`: the number of newline bytes in the file (physical lines, comments and blanks
# included). The previous reading used `Get-Content | Measure-Object -Line`, which counts non-empty
# lines only; both are reported per category in the reading page so the difference is visible.

Set-Location 'D:\Projects\Camel-wt\witness'
[Environment]::CurrentDirectory = (Get-Location).Path

function Wc([string]$p) { ([regex]::Matches([System.IO.File]::ReadAllText((Join-Path 'D:\Projects\Camel-wt\witness' $p)), "`n")).Count }
function Decls([string]$p, [string]$pat) { (Select-String -Path (Join-Path 'D:\Projects\Camel-wt\witness' $p) -Pattern $pat | Measure-Object).Count }

$rows = New-Object System.Collections.Generic.List[string]
$rows.Add('category,file,lines,decl_kind,decl_count,replaces_cxx_file(s)')

function Add-Cat([string]$cat, [string[]]$files, [hashtable]$repl) {
  foreach ($f in $files) {
    if (-not $f) { continue }
    $kind = '—'; $count = '—'
    if ($f -like '*.cml') {
      $r = Decls $f '^export rule'; $fn = Decls $f '^export func'
      $kind = 'export rule / export func'; $count = "$r / $fn"
    }
    $r2 = '— (this file is the C++ side)'
    if ($repl -and $repl.ContainsKey($f)) { $r2 = $repl[$f] }
    $line = "$cat,$f,$(Wc $f),$kind,$count," + '"' + ($r2 -replace '"','""') + '"'
    $rows.Add($line)
  }
}

$adRules = @(git ls-files 'src/stdlib/autodiff/*')
$adRules += @('src/stdlib/transforms/jvp.cpp','src/stdlib/transforms/split.cpp')
Add-Cat 'autodiff_cpp' $adRules $null

Add-Cat 'autodiff_rulesets_cml' @('src/stdlib/rulesets/jvp.cml','src/stdlib/rulesets/transpose.cml','src/stdlib/rulesets/vmap.cml') @{
  'src/stdlib/rulesets/jvp.cml'       = 'src/stdlib/transforms/jvp.cpp + src/stdlib/transforms/jvp_operator.cpp'
  'src/stdlib/rulesets/transpose.cml' = 'src/stdlib/transforms/transpose/** (cotangent, rules, walk, walkmachinery, signature, entry)'
  'src/stdlib/rulesets/vmap.cml'      = 'src/stdlib/transforms/vmap.cpp + src/stdlib/transforms/vmap_operator.cpp'
}

Add-Cat 'quant_ruleset_cml' @('src/stdlib/rulesets/int8.cml') @{
  'src/stdlib/rulesets/int8.cml' = '— (no C++ counterpart in this tree: the int8 rule set is Lang-only; no int8 or quant file exists under src/stdlib besides it)'
}

$onnx = @(git ls-files 'src/stdlib/onnx/*'); $onnx += @('src/stdlib/onnx.cml')
Add-Cat 'onnx_export' $onnx $null

Add-Cat 'rules_rewrite' @(git ls-files 'src/stdlib/rewrite/*') $null
Add-Cat 'cxx_opt_lang' @(git ls-files 'src/stdlib/std/cxx/*') $null

Add-Cat 'opt_cml' @('src/stdlib/opt.cml') @{
  'src/stdlib/opt.cml' = 'src/stdlib/std/cxx/opt/** (the four rewrites: generic/*, inline/engine.cpp, constant_bytes.cpp)'
}

$out = 'internal/reports/cc-eval-prelim/2026-11-28/A-libraries-files.csv'
Set-Content -Path $out -Value ($rows -join "`r`n") -Encoding utf8
"rows written: " + ($rows.Count - 1)

# per-category sums, for the check against A-libraries.csv
$byCat = @{}
foreach ($line in $rows[1..($rows.Count-1)]) {
  $p = $line -split ','
  if (-not $byCat.ContainsKey($p[0])) { $byCat[$p[0]] = 0 }
  $byCat[$p[0]] += [int]$p[2]
}
"=== 逐类行数之和（wc -l） ==="
foreach ($k in $byCat.Keys | Sort-Object) { "{0,-24} {1,6}" -f $k, $byCat[$k] }
