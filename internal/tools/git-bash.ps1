# Finds Git for Windows' bash (the MSYS one), for the tests that run shell fragments on this machine.
#
# `Get-Command bash` is not used because on a Windows machine the first `bash` on PATH may be
# `C:\Windows\System32\bash.exe`, the WSL launcher. It cannot open the `C:/...` paths these tests hand
# it and has a different /proc, so a test that meant to check a shell fragment would fail, or skip
# its behaviour checks, for a reason that has nothing to do with the fragment. Git's bash reads
# Windows paths written with forward slashes, which is what the tests rely on.
#
# Dot-source this file; `Get-GitBash` returns the full path of Git's bash.exe, or $null when Git for
# Windows (or its bash) is not found. Nothing under the Windows system directory is ever returned.

function Select-GitBash([string[]]$Candidates) {
    $system = @($env:SystemRoot, $env:windir) | Where-Object { $_ } |
        ForEach-Object { ($_ -replace '/', '\').TrimEnd('\') } | Select-Object -Unique
    foreach ($c in $Candidates) {
        if (-not $c) { continue }
        $full = $c -replace '/', '\'
        $inSystem = $false
        foreach ($s in $system) {
            if ($full.StartsWith($s + '\', [StringComparison]::OrdinalIgnoreCase)) { $inSystem = $true }
        }
        if ($inSystem) { continue }
        if (Test-Path -LiteralPath $c -PathType Leaf) { return $c }
    }
    return $null
}

function Get-GitBash {
    $roots = @()
    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($git) {
        # `git --exec-path` is <root>/mingw64/libexec/git-core (or <root>/libexec/git-core): the Git root
        # is the nearest ancestor that holds a bin or usr\bin directory with a bash in it.
        $exec = (& git --exec-path 2>$null | Select-Object -First 1)
        if ($exec) {
            $dir = ($exec -replace '/', '\').TrimEnd('\')
            while ($dir -and ($dir -ne (Split-Path $dir -Parent))) {
                $roots += $dir
                $dir = Split-Path $dir -Parent
            }
        }
        # `git.exe` itself sits in <root>\cmd, <root>\bin or <root>\mingw64\bin.
        $dir = Split-Path (Split-Path $git.Source -Parent) -Parent
        while ($dir -and ($dir -ne (Split-Path $dir -Parent))) {
            $roots += $dir
            $dir = Split-Path $dir -Parent
        }
    }
    $candidates = @()
    foreach ($r in ($roots | Select-Object -Unique)) {
        $candidates += (Join-Path $r 'bin\bash.exe')
        $candidates += (Join-Path $r 'usr\bin\bash.exe')
    }
    return (Select-GitBash $candidates)
}
