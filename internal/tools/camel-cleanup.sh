#!/usr/bin/env bash
# camel-cleanup.sh -- the record a run leaves when it creates something, and the only door a
# cleanup may go through.
#
# Rule 12 of internal/collab/rules.md, and the two accidents behind it:
#
#   * the coordinator deleted a lane checkout that was still waiting for CI, because the test was
#     `ahead==0 && status clean` -- which cannot tell "pushed and finished" from "pushed and
#     working";
#   * a lane deleted the shared staging bare `~/camel-work/Camel.git` while clearing a host
#     temporary checkout.
#
# Both criteria were "looks unused", and both things had a user. So the criterion here is the
# record written at creation time and nothing else:
#
#   root       $HOME/camel-work/tmp                    the only place a run may put a temporary
#                                                      checkout, replay tree or sandbox
#   run dir    $HOME/camel-work/tmp/<lane>-<time>/     one per run
#   manifest   $HOME/camel-work/manifests/<lane>-<time>.tsv
#                                                      outside the root, so clearing the root
#                                                      cannot delete the record it is judged by
#
# `rm` accepts a path only when, after resolving `..`, an absolute spelling and every symlink to a
# real path, it is BOTH under the root AND named in a manifest. Everything else is refused and the
# path is printed. `~/camel-work/Camel.git` is outside the root, so this script cannot reach it at
# all: the refusal is structural, not a special case for that name.
#
# Manifest format -- TSV, a header block, then one line per artifact:
#
#   #camel-cleanup-manifest 1
#   runId	<lane>-<time>
#   lane	<lane>
#   createdBy	<who>
#   createdAt	<ISO 8601>
#   host	<hostname>
#   root	<absolute root>
#   runDir	<absolute run dir>
#   manifest	<absolute path of this file>
#   #createdAt	kind	path	purpose
#   2026-11-24T02:40:00+08:00	checkout	<home>/camel-work/tmp/<lane>/src	built once, removed by prune
#
# TSV rather than JSON because the deleting side is a POSIX shell on the build host, where no JSON
# tool is installed; awk reads this and a person reads this, and one line per artifact diffs.
#
# Commands
#   init   <lane> [--purpose TEXT]            create a run dir and its manifest; prints runId,
#                                             runDir and manifest as `key<TAB>value` lines
#   add    <runId> <path> [--kind K] [--purpose TEXT]
#                                             record a path this run created (must be under its
#                                             run dir; a symlink or an outside path is refused)
#   rm     <path> [--run RUNID] [--what-if]   remove a path that is under the root and recorded
#   prune  <runId> [--what-if]                remove every recorded path of a run, then the run
#                                             dir if it is empty; the manifest stays
#   list                                      every manifest, with whether its run dir exists
#   show   <runId>                            print a manifest
#   allow  <path> [--run RUNID]               the same judgement as `rm`, no deletion (0 / 3)
#
# Exit codes: 0 done, 1 failed, 2 usage, 3 refused.
#
# Overrides (all optional): --home DIR, --root DIR, --manifests DIR, or the environment variables
# CAMEL_CLEANUP_HOME / CAMEL_CLEANUP_ROOT / CAMEL_CLEANUP_MANIFESTS.

set -u

cc_say() { printf '%s\n' "$*" >&2; }
cc_out() { printf '%s\n' "$*"; }

cc_usage() {
    cc_say 'usage: camel-cleanup.sh init <lane> [--purpose TEXT]'
    cc_say '       camel-cleanup.sh add <runId> <path> [--kind KIND] [--purpose TEXT]'
    cc_say '       camel-cleanup.sh rm <path> [--run RUNID] [--what-if]'
    cc_say '       camel-cleanup.sh prune <runId> [--what-if]'
    cc_say '       camel-cleanup.sh list | show <runId> | allow <path> [--run RUNID]'
    exit 2
}

# --- where the root, the manifests and the record live -----------------------
cc_home() {
    local h="${CC_HOME:-${CAMEL_CLEANUP_HOME:-${HOME:-}}}"
    [ -n "$h" ] || { cc_say '[camel-cleanup] no home directory; give --home'; exit 2; }
    printf '%s' "$h"
}

cc_root() {
    if [ -n "${CC_ROOT:-${CAMEL_CLEANUP_ROOT:-}}" ]; then
        printf '%s' "${CC_ROOT:-$CAMEL_CLEANUP_ROOT}"
    else
        printf '%s/camel-work/tmp' "$(cc_home)"
    fi
}

cc_manifests() {
    if [ -n "${CC_MANIFESTS:-${CAMEL_CLEANUP_MANIFESTS:-}}" ]; then
        printf '%s' "${CC_MANIFESTS:-$CAMEL_CLEANUP_MANIFESTS}"
    else
        printf '%s/camel-work/manifests' "$(cc_home)"
    fi
}

# --- resolving a path to what it really is -----------------------------------
# Every refusal below happens on the resolved path, never on the spelling: `tmp/x/../../Camel.git`
# and `<home>/camel-work/tmp/x/../../../Camel.git` are one path, and a prefix comparison on the
# spelling is exactly the implementation that a `..` walks around.
cc_real() {
    local p="$1"
    [ -n "$p" ] || return 1
    case "$p" in -*) return 1 ;; esac          # an option is never a path
    readlink -f -- "$p" 2>/dev/null
}

# $1 = real path, $2 = real root: is $1 strictly under $2?
cc_under() {
    case "$1" in
        "$2"/?*) return 0 ;;
        *) return 1 ;;
    esac
}

cc_is_symlink() {
    local p="$1"
    while [ "${p%/}" != "$p" ] && [ "$p" != '/' ]; do p="${p%/}"; done
    [ -L "$p" ]
}

# --- the manifest ------------------------------------------------------------
cc_manifest_path() { printf '%s/%s.tsv' "$(cc_manifests)" "$1"; }

cc_manifest_field() { # <runId> <field>
    local f
    f=$(cc_manifest_path "$1")
    [ -f "$f" ] || return 1
    awk -F '\t' -v k="$2" '$1==k { print $2; exit }' "$f"
}

cc_run_dir() { # <runId> -> the run dir as the manifest recorded it
    local d
    d=$(cc_manifest_field "$1" runDir) || return 1
    [ -n "$d" ] || return 1
    printf '%s' "$d"
}

# The manifest that names this real path, optionally restricted to one run. Prints the manifest.
cc_recorded() { # <real path> [runId]
    local want="$1" run="${2:-}" f hit
    for f in "$(cc_manifests)"/*.tsv; do
        [ -f "$f" ] || continue
        if [ -n "$run" ] && [ "$(basename "$f" .tsv)" != "$run" ]; then continue; fi
        hit=$(awk -F '\t' -v want="$want" \
            '!/^#/ && NF>=3 && $3==want { print 1; exit }' "$f")
        if [ "$hit" = 1 ]; then printf '%s' "$f"; return 0; fi
    done
    return 1
}

cc_refuse() { # <path as given> <reason> [<resolved>]
    if [ -n "${3:-}" ]; then
        cc_say "[camel-cleanup] refused: $1 -- $2 (resolved: $3)"
    else
        cc_say "[camel-cleanup] refused: $1 -- $2"
    fi
    return 3
}

# --- init --------------------------------------------------------------------
cmd_init() {
    local lane="${1:-}" purpose="${2:-}"
    [ -n "$lane" ] || cc_usage
    lane=$(printf '%s' "$lane" | tr -cd 'A-Za-z0-9._-')
    [ -n "$lane" ] || { cc_say '[camel-cleanup] a lane name needs at least one of A-Za-z0-9._-'; exit 2; }

    local root manifests base run n=1
    root=$(cc_root)
    manifests=$(cc_manifests)
    mkdir -p "$root" "$manifests" || { cc_say "[camel-cleanup] cannot create $root or $manifests"; exit 1; }
    root=$(cd "$root" && pwd -P) || exit 1
    manifests=$(cd "$manifests" && pwd -P) || exit 1

    base="$lane-$(date +%Y%m%d-%H%M%S)"
    run="$base"
    while [ -e "$root/$run" ] || [ -e "$manifests/$run.tsv" ]; do
        n=$((n + 1))
        run="$base-$n"
    done
    mkdir -p "$root/$run" || { cc_say "[camel-cleanup] cannot create $root/$run"; exit 1; }

    local manifest="$manifests/$run.tsv"
    {
        printf '#camel-cleanup-manifest\t1\n'
        printf 'runId\t%s\n' "$run"
        printf 'lane\t%s\n' "$lane"
        printf 'createdBy\t%s\n' "${CAMEL_CLEANUP_BY:-$(id -un 2>/dev/null || echo unknown)}"
        printf 'createdAt\t%s\n' "$(date -Is)"
        printf 'host\t%s\n' "$(hostname 2>/dev/null || echo unknown)"
        printf 'root\t%s\n' "$root"
        printf 'runDir\t%s\n' "$root/$run"
        printf 'manifest\t%s\n' "$manifest"
        [ -n "$purpose" ] && printf 'purpose\t%s\n' "$purpose"
        printf '#createdAt\tkind\tpath\tpurpose\n'
    } > "$manifest" || { cc_say "[camel-cleanup] cannot write $manifest"; exit 1; }

    printf 'runId\t%s\n' "$run"
    printf 'runDir\t%s\n' "$root/$run"
    printf 'manifest\t%s\n' "$manifest"
}

# --- add ---------------------------------------------------------------------
cmd_add() {
    local run="${1:-}" given="${2:-}" kind="${3:-artifact}" purpose="${4:-}"
    [ -n "$run" ] && [ -n "$given" ] || cc_usage

    local f rundir real
    f=$(cc_manifest_path "$run")
    [ -f "$f" ] || { cc_say "[camel-cleanup] no manifest for run $run ($f)"; exit 2; }
    rundir=$(cc_run_dir "$run") || { cc_say "[camel-cleanup] manifest $f names no runDir"; exit 1; }
    rundir=$(cc_real "$rundir") || rundir=""

    if cc_is_symlink "$given"; then
        cc_refuse "$given" 'is a symlink; a record names a directory or file, not what it points at' "$(cc_real "$given")"
        exit 3
    fi
    real=$(cc_real "$given")
    [ -n "$real" ] || { cc_refuse "$given" 'cannot be resolved (missing, or a symlink loop)'; exit 3; }
    [ -n "$rundir" ] && cc_under "$real" "$rundir" || {
        cc_refuse "$given" "is not under this run's dir ($rundir); a run may only record what it created there" "$real"
        exit 3
    }
    [ "$real" != "$rundir" ] || {
        cc_refuse "$given" 'is the run dir itself; record its contents, not the container' "$real"
        exit 3
    }

    if awk -F '\t' -v want="$real" '!/^#/ && NF>=3 && $3==want { found=1 } END { exit found?0:1 }' "$f"; then
        cc_say "[camel-cleanup] already recorded: $real"
        exit 0
    fi

    printf '%s\t%s\t%s\t%s\n' "$(date -Is)" "$kind" "$real" "$purpose" >> "$f" ||
        { cc_say "[camel-cleanup] cannot append to $f"; exit 1; }
    printf 'recorded\t%s\t%s\n' "$kind" "$real"
}

# --- the judgement both `rm` and `allow` use --------------------------------
# Sets CC_JUDGED to the resolved path. Returns 0 when the path may be removed, 3 when not (having
# printed the path and the reason).
cc_judge() {
    local given="$1" run="${2:-}" real root
    CC_JUDGED=''

    if cc_is_symlink "$given"; then
        cc_refuse "$given" 'is a symlink; the record names a directory, not what it points at' "$(cc_real "$given")"
        return 3
    fi
    real=$(cc_real "$given")
    if [ -z "$real" ]; then
        cc_refuse "$given" 'cannot be resolved (missing, or a symlink loop)'
        return 3
    fi
    root=$(cc_root)
    root=$(cd "$root" 2>/dev/null && pwd -P) || root=''

    if [ -z "$root" ]; then
        cc_refuse "$given" "the root $(cc_root) does not exist, so nothing is under it" "$real"
        return 3
    fi
    if ! cc_under "$real" "$root"; then
        cc_refuse "$given" "is outside the cleanup root $root" "$real"
        return 3
    fi
    if [ "$real" = "$root" ]; then
        cc_refuse "$given" 'is the cleanup root itself' "$real"
        return 3
    fi
    if ! cc_recorded "$real" "$run" >/dev/null; then
        if [ -n "$run" ]; then
            cc_refuse "$given" "is under the root but not named in run $run's manifest" "$real"
        else
            cc_refuse "$given" 'is under the root but not named in any manifest' "$real"
        fi
        return 3
    fi
    CC_JUDGED="$real"
    return 0
}

cmd_rm() {
    local given="${1:-}" run="${2:-}" whatif="${3:-0}"
    [ -n "$given" ] || cc_usage
    cc_judge "$given" "$run" || exit 3

    local real="$CC_JUDGED" m
    m=$(cc_recorded "$real" "$run") || m=''
    if [ ! -e "$real" ] && [ ! -L "$real" ]; then
        cc_say "[camel-cleanup] already gone: $real (recorded in ${m:-a manifest})"
        exit 0
    fi
    if [ "$whatif" = 1 ]; then
        cc_say "[camel-cleanup] would remove: $real (recorded in ${m:-a manifest})"
        exit 0
    fi
    rm -rf -- "$real" || { cc_say "[camel-cleanup] rm failed: $real"; exit 1; }
    if [ -e "$real" ] || [ -L "$real" ]; then
        cc_say "[camel-cleanup] still there after rm -rf: $real"
        exit 1
    fi
    cc_say "[camel-cleanup] removed: $real (recorded in ${m:-a manifest})"
    printf 'removed\t%s\n' "$real"
}

cmd_allow() {
    local given="${1:-}" run="${2:-}"
    [ -n "$given" ] || cc_usage
    cc_judge "$given" "$run" || exit 3
    printf '%s\n' "$CC_JUDGED"
    exit 0
}

# --- prune -------------------------------------------------------------------
cmd_prune() {
    local run="${1:-}" whatif="${2:-0}"
    [ -n "$run" ] || cc_usage
    local f rundir real n=0 removed=0
    f=$(cc_manifest_path "$run")
    [ -f "$f" ] || { cc_say "[camel-cleanup] no manifest for run $run ($f)"; exit 2; }
    rundir=$(cc_run_dir "$run") || { cc_say "[camel-cleanup] manifest $f names no runDir"; exit 1; }

    # Children before parents: the entries were appended in creation order, and a directory may
    # have been recorded after the file inside it.
    while IFS= read -r real; do
        [ -n "$real" ] || continue
        n=$((n + 1))
        if [ ! -e "$real" ] && [ ! -L "$real" ]; then
            cc_say "[camel-cleanup] already gone: $real"
            continue
        fi
        if [ "$whatif" = 1 ]; then
            cc_say "[camel-cleanup] would remove: $real (run $run)"
            continue
        fi
        rm -rf -- "$real" || { cc_say "[camel-cleanup] rm failed: $real"; exit 1; }
        [ -e "$real" ] || [ -L "$real" ] && { cc_say "[camel-cleanup] still there after rm -rf: $real"; exit 1; }
        cc_say "[camel-cleanup] removed: $real (run $run)"
        removed=$((removed + 1))
    done <<EOF
$(awk -F '\t' '!/^#/ && NF>=3 { print $3 }' "$f" | tac)
EOF

    if [ "$whatif" = 1 ]; then
        printf 'prune\t%s\t%d recorded, %d would be removed\n' "$run" "$n" "$n"
        exit 0
    fi
    # The run dir goes only when it is empty: anything a run left behind that it never recorded is
    # not this mechanism's to delete, and `rmdir` refusing is how that is enforced.
    if [ -d "$rundir" ]; then
        if rmdir -- "$rundir" 2>/dev/null; then
            cc_say "[camel-cleanup] removed the empty run dir: $rundir"
        else
            cc_say "[camel-cleanup] kept $rundir: it is not empty, and what is in it was never recorded"
        fi
    fi
    printf 'prune\t%s\t%d recorded, %d removed (manifest kept at %s)\n' "$run" "$n" "$removed" "$f"
}

# --- list / show -------------------------------------------------------------
cmd_list() {
    local f run rundir n
    for f in "$(cc_manifests)"/*.tsv; do
        [ -f "$f" ] || continue
        run=$(basename "$f" .tsv)
        rundir=$(cc_manifest_field "$run" runDir)
        n=$(awk -F '\t' '!/^#/ && NF>=3' "$f" | wc -l | tr -d ' ')
        if [ -d "$rundir" ]; then state=present; else state=gone; fi
        printf '%s\t%s\t%s\t%s\n' "$run" "$state" "$n" "$f"
    done
}

cmd_show() {
    local run="${1:-}"
    [ -n "$run" ] || cc_usage
    local f
    f=$(cc_manifest_path "$run")
    [ -f "$f" ] || { cc_say "[camel-cleanup] no manifest for run $run ($f)"; exit 2; }
    cat -- "$f"
}

# --- argument parsing --------------------------------------------------------
# Hand-rolled so that a `--home` given before or after the command both work and a value that
# starts with `-` is still refused as a path rather than parsed as an option.
cc_main() {
    local words=() whatif=0 i w
    while [ $# -gt 0 ]; do
        w="$1"
        case "$w" in
            --home) CC_HOME="${2:-}"; shift 2 ;;
            --home=*) CC_HOME="${w#*=}"; shift ;;
            --root) CC_ROOT="${2:-}"; shift 2 ;;
            --root=*) CC_ROOT="${w#*=}"; shift ;;
            --manifests) CC_MANIFESTS="${2:-}"; shift 2 ;;
            --manifests=*) CC_MANIFESTS="${w#*=}"; shift ;;
            --run) words+=(--run "${2:-}"); shift 2 ;;
            --run=*) words+=(--run "${w#*=}"); shift ;;
            --kind) words+=(--kind "${2:-}"); shift 2 ;;
            --kind=*) words+=(--kind "${w#*=}"); shift ;;
            --purpose) words+=(--purpose "${2:-}"); shift 2 ;;
            --purpose=*) words+=(--purpose "${w#*=}"); shift ;;
            --what-if) whatif=1; shift ;;
            -h|--help) cc_usage ;;
            --) shift; while [ $# -gt 0 ]; do words+=("$1"); shift; done ;;
            *) words+=("$w"); shift ;;
        esac
    done

    [ ${#words[@]} -gt 0 ] || cc_usage
    local cmd="${words[0]}"
    local args=() run='' kind='artifact' purpose=''
    i=1
    while [ $i -lt ${#words[@]} ]; do
        case "${words[$i]}" in
            --run) run="${words[$((i+1))]:-}"; i=$((i + 2)) ;;
            --kind) kind="${words[$((i+1))]:-}"; i=$((i + 2)) ;;
            --purpose) purpose="${words[$((i+1))]:-}"; i=$((i + 2)) ;;
            *) args+=("${words[$i]}"); i=$((i + 1)) ;;
        esac
    done

    case "$cmd" in
        init)  cmd_init "${args[0]:-}" "$purpose" ;;
        add)   [ ${#args[@]} -ge 2 ] || cc_usage; cmd_add "${args[0]}" "${args[1]}" "$kind" "$purpose" ;;
        rm)    [ ${#args[@]} -ge 1 ] || cc_usage; cmd_rm "${args[0]}" "$run" "$whatif" ;;
        allow) [ ${#args[@]} -ge 1 ] || cc_usage; cmd_allow "${args[0]}" "$run" ;;
        prune) [ ${#args[@]} -ge 1 ] || cc_usage; cmd_prune "${args[0]}" "$whatif" ;;
        list)  cmd_list ;;
        show)  [ ${#args[@]} -ge 1 ] || cc_usage; cmd_show "${args[0]}" ;;
        *)     cc_say "[camel-cleanup] unknown command: $cmd"; cc_usage ;;
    esac
}

# Sourced by a test or another script: define the functions and stop. Run as a program: dispatch.
if [ "${BASH_SOURCE[0]:-$0}" = "$0" ]; then
    cc_main "$@"
fi