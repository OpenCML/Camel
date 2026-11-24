# Machine-local settings for the gate tools in this directory.
#
# Every value that names one machine -- the ssh alias of the build host, the ssh/scp binaries, the
# directory the panel's state files live in, the layout under the host's home directory -- is read
# from one JSON file that is not in version control:
#
#   internal/tools/camel-gate.local.json       this machine's values (git-ignored)
#   internal/tools/camel-gate.example.json     the shape of that file, placeholders only
#
# The rules this file exists to keep:
#   * a machine-specific value appears in the local file only -- never in a script, a comment or the
#     example, so the repository can be read (and reviewed) without knowing where it runs;
#   * a tool that needs a setting refuses to run when the setting is absent instead of guessing
#     (exit 2, and the message names the file to create);
#   * a command-line parameter of the same name overrides the file for one run.
#
# A key that the file does not carry keeps the default below. The defaults are the generic host
# layout of the Camel build host (a home directory with camel-work/ and camel-eval/ under it); they
# name no machine, so a fresh clone works once the local file says how to reach the host.

function Get-CamelGateConfig {
    [CmdletBinding()]
    param(
        # Path of the local file; empty means "<this directory>/camel-gate.local.json".
        [string]$LocalConfig = '',
        # Directory this file and its callers live in.
        [string]$ScriptRoot = '',
        # Per-run overrides: parameter name -> value. An empty value means "not given".
        [hashtable]$Override = @{}
    )

    if (-not $ScriptRoot) { $ScriptRoot = $PSScriptRoot }
    $path = if ($LocalConfig) { $LocalConfig } else { Join-Path $ScriptRoot 'camel-gate.local.json' }

    $file = $null
    if (Test-Path -LiteralPath $path) {
        try {
            $file = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
        } catch {
            Write-Host "[camel-gate] cannot read $path as JSON: $_"
            exit 2
        }
    }

    $settings = [ordered]@{
        # The host: how to reach it and what it calls the two programs that do.
        sshHost          = ''
        sshExe           = 'ssh'
        scpExe           = 'scp'
        # The workstation: the directory that holds build-state.json, test-state.json and
        # run-registry.jsonl -- the three files the panel reads.
        stateDir         = ''
        # The host's home directory layout, all relative to $HOME there.
        hostWorkRoot     = 'camel-work'
        hostBareRepo     = 'camel-work/Camel.git'
        hostClangEnv     = 'camel-eval/conda/envs/clang'
        hostBuildEnv     = 'camel-eval/venv-build/bin'
        hostBenchEnv     = 'camel-eval/venv-bench/bin'
        hostLocalBin     = '.local/bin'
        # The toolchain's libstdc++, for host checkouts built before the install carried its own
        # copy with a $ORIGIN rpath (see camel-remote.ps1's header).
        hostLibStdCxxDir = ''
        # The checkout watch-fsh-dev.ps1 follows.
        watchRepo        = ''
        # The one worktree allowed to push `develop` (scripts/hooks/pre-push). One absolute path,
        # or several separated by ';' when a machine keeps the merge tree in more than one place.
        # Empty means no tree may push develop -- the hook refuses and says what to write, because
        # a rule it cannot read must not default to "allow".
        mergeWorktree    = ''
        # The worktree a merge position measures its baseline in (merge-lane.ps1): the tree that is
        # NOT the one being compared, built once and kept warm between merges. One absolute path,
        # which the merge tool creates when it is not there yet. Empty means no baseline tree is
        # named -- the merge refuses to start and says what to write, because a baseline measured in
        # "some path" is a reading about somebody else's tree that looks exactly like a reading about
        # this one.
        baselineWorktree = ''
    }

    if ($null -ne $file) {
        foreach ($key in @($settings.Keys)) {
            $value = $file.PSObject.Properties[$key]
            if ($null -eq $value) { continue }
            # A key that takes more than one value (mergeWorktree: the merge tree on more than one
            # drive) is written as a JSON array and kept here as one ';'-separated string, so a
            # caller prints it instead of "System.Object[]".
            $text = if ($value.Value -is [System.Array]) {
                (@($value.Value) | ForEach-Object { "$_" }) -join ';'
            } else {
                "$($value.Value)"
            }
            if ($text.Trim()) { $settings[$key] = $text }
        }
        # An unknown key is a typo or a leftover, and silently ignoring it is how a machine keeps
        # running with a setting its owner believes changed. A key that begins with '_' is a comment:
        # camel-gate.example.json carries one, that file's instruction is "copy this file", and a
        # copy that the loader refused would make the instruction false.
        foreach ($property in $file.PSObject.Properties) {
            if ($property.Name.StartsWith('_')) { continue }
            if (-not $settings.Contains($property.Name)) {
                Write-Host "[camel-gate] $path has an unknown key '$($property.Name)'; known keys: $($settings.Keys -join ', ')"
                exit 2
            }
        }
    }

    foreach ($key in @($Override.Keys)) {
        if ($Override[$key]) { $settings[$key] = "$($Override[$key])" }
    }

    $settings.localConfigPath = $path
    $settings.localConfigFound = ($null -ne $file)
    return [pscustomobject]$settings
}

# The one message a tool prints when a setting it cannot guess is missing: the file to create, the
# key it wanted, and the example that shows the shape.
function Stop-CamelGateConfig([string]$Tool, [object]$Config, [string]$Key) {
    Write-Host "[$Tool] the local settings file has no '$Key': $($Config.localConfigPath)"
    Write-Host "[$Tool] copy internal/tools/camel-gate.example.json to that path and fill it in (it is git-ignored)"
    exit 2
}