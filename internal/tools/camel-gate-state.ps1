# Publishing one gate state file without ever taking a run down with it.
#
# `camel-build.ps1` and `camel-test.ps1` publish `build-state.json` / `test-state.json` for the desk
# panel, and the panel and other agents read those files constantly. The files are advisory --
# `internal/tools/README.md`, "The panel's files": a locked or unwritable file never fails a build or
# a plan -- but each helper wrote through `Move-Item -Force`, and on 2026-11-25 that threw
# ERROR_ALREADY_EXISTS (183, "cannot create a file when that file already exists") while a reader had
# the destination open. The exception escaped from a progress line AFTER `cmake --build` had finished,
# and the helper exited 1 on a build that `cmake --build . -- -n` reported as `ninja: no work to do`:
# a build that succeeded, reported as a build that failed, which is how the first build of a lane was
# killed that night (C-1124-366).
#
# So the write is one function with three properties:
#
#   * atomic. The JSON goes to `<state>.<pid>.tmp` in the same directory as the destination and is
#     moved over it in one rename: `File.Move`'s overwrite overload, which on Windows is MoveFileEx
#     with MOVEFILE_REPLACE_EXISTING, one directory entry changed within one volume (the temp file is
#     beside the destination, so a rename cannot become a copy). A reader sees the old document or the
#     new one, never a half-written one.
#   * patient. A reader that has the destination open makes Windows refuse the rename (the reader
#     above is one `Get-Content` away). Readers here hold the file for milliseconds, so retry briefly
#     -- 25 attempts, 40 ms apart, the same window `camel-remote.ps1#Edit-State` uses on the host --
#     before dropping this one update.
#   * silent about failure. `$false` is returned, never an exception, so the caller decides what to
#     say and the run's exit code stays its own. The caller drops the update; the next progress line
#     publishes the newer state anyway.

function Publish-CamelGateState {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Json,
        [int]$Attempts = 25,
        [int]$RetryDelayMs = 40
    )

    $tmp = "$Path.$PID.tmp"
    try {
        Set-Content -Path $tmp -Value $Json -Encoding utf8 -ErrorAction Stop
    } catch {
        Remove-Item -Path $tmp -Force -ErrorAction SilentlyContinue
        return $false
    }

    for ($attempt = 0; $attempt -lt $Attempts; $attempt++) {
        try {
            if ($PSVersionTable.PSVersion.Major -ge 6) {
                [System.IO.File]::Move($tmp, $Path, $true)
            } elseif (Test-Path -LiteralPath $Path) {
                # Windows PowerShell's .NET has no overwrite overload; ReplaceFile is the atomic
                # replacement there, with a backup name it requires -- removed right after.
                [System.IO.File]::Replace($tmp, $Path, "$Path.old", $true)
                Remove-Item -Path "$Path.old" -Force -ErrorAction SilentlyContinue
            } else {
                [System.IO.File]::Move($tmp, $Path)
            }
            return $true
        } catch {
            if ($attempt -eq $Attempts - 1) {
                Remove-Item -Path $tmp -Force -ErrorAction SilentlyContinue
                return $false
            }
            Start-Sleep -Milliseconds $RetryDelayMs
        }
    }
    return $false
}