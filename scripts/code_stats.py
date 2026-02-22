#!/usr/bin/env python3
"""
Use pydriller to show code churn and cumulative total for a branch (window chart).
Stats are computed by traversing ALL commits in branch history; only first-parent
commits are drawn and tags are shown below. Single positional arg: branch (default main).
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

# Paths under these prefixes are excluded from line counts (use forward slash)
EXCLUDE_PREFIXES = ("third_party/",)

# File suffix -> language label for breakdown (lowercase suffix including dot)
EXT_TO_LANG: dict[str, str] = {
    ".c": "C/C++", ".cpp": "C/C++",
    ".h": "C/C++",
    ".cml": "Camel",
    ".py": "Python",
    ".js": "JavaScript",
    ".ts": "TypeScript",
    ".sh": "Shell", ".bash": "Shell",
    ".md": "Markdown",
}
LANG_ORDER = ("C/C++", "Camel", "JavaScript", "Python", "TypeScript", "Shell", "Markdown")


def _path_to_lang(file_path: str | None) -> str:
    """Return language label for path by suffix; 'Other' if unknown or no suffix."""
    if not file_path:
        return "Other"
    norm = file_path.replace("\\", "/")
    if "/" in norm:
        name = norm.rsplit("/", 1)[-1]
    else:
        name = norm
    if "." in name:
        ext = "." + name.rsplit(".", 1)[-1].lower()
        return EXT_TO_LANG.get(ext, "Other")
    return "Other"


def _excluded(file_path: str | None) -> bool:
    if not file_path:
        return True
    norm = file_path.replace("\\", "/")
    return any(norm.startswith(p) for p in EXCLUDE_PREFIXES)


def _file_ins_dels(m) -> tuple[int, int]:
    """Get (additions, deletions) for a ModifiedFile; fallback to diff_parsed or 0."""
    ins = getattr(m, "added_lines", None)
    dels = getattr(m, "deleted_lines", None)
    if ins is not None and dels is not None:
        try:
            return (int(ins), int(dels))
        except (TypeError, ValueError):
            pass
    diff = getattr(m, "diff_parsed", None) or {}
    if isinstance(diff, dict):
        a = diff.get("added") or []
        d = diff.get("deleted") or []
        return (len(a), len(d))
    return (0, 0)


def _commit_ins_dels(commit, exclude_paths: bool = True) -> tuple[int, int]:
    """Get (insertions, deletions) for a commit; exclude EXCLUDE_PREFIXES when exclude_paths."""
    ins, dels = 0, 0
    if commit.modified_files:
        for m in commit.modified_files:
            if exclude_paths:
                path = m.new_path or m.old_path
                if _excluded(path):
                    continue
            a, b = _file_ins_dels(m)
            ins += a
            dels += b
    # When excluding paths, never use commit-level fallback: it would include excluded dirs
    if not exclude_paths and ins == 0 and dels == 0:
        ins = getattr(commit, "insertions", None) or 0
        dels = getattr(commit, "deletions", None) or 0
        try:
            ins, dels = int(ins), int(dels)
        except (TypeError, ValueError):
            ins, dels = 0, 0
    return (ins, dels)


def _commit_ins_dels_by_lang(commit) -> dict[str, tuple[int, int]]:
    """Per-language (ins, dels) for commit; excludes EXCLUDE_PREFIXES and Other."""
    by_lang: dict[str, tuple[int, int]] = {}
    if not commit.modified_files:
        return by_lang
    for m in commit.modified_files:
        path = m.new_path or m.old_path
        if _excluded(path):
            continue
        lang = _path_to_lang(path)
        if lang == "Other":
            continue
        a, b = _file_ins_dels(m)
        if lang not in by_lang:
            by_lang[lang] = (0, 0)
        prev = by_lang[lang]
        by_lang[lang] = (prev[0] + a, prev[1] + b)
    return by_lang


def _git_first_parent_commits(repo_path: Path, branch: str) -> list[str]:
    """Return commit hashes on branch in first-parent order, oldest first."""
    r = subprocess.run(
        ["git", "log", "--first-parent", "--format=%H", branch],
        cwd=repo_path,
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        raise RuntimeError("git log failed: %s" % (r.stderr or r.stdout))
    hashes = [h.strip() for h in r.stdout.strip().splitlines() if h.strip()]
    hashes.reverse()
    return hashes


def _expand_hash(repo_path: Path, short_hash: str) -> str:
    """Return full 40-char hash; if rev-parse fails, return short_hash as-is."""
    r = subprocess.run(
        ["git", "rev-parse", short_hash],
        cwd=repo_path,
        capture_output=True,
        text=True,
    )
    if r.returncode == 0 and r.stdout.strip():
        return r.stdout.strip().lower()
    return short_hash.strip().lower()


def _git_tags_at(repo_path: Path, commit_hash: str) -> list[str]:
    """Return tag names that point at the given commit."""
    r = subprocess.run(
        ["git", "tag", "--points-at", commit_hash],
        cwd=repo_path,
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        return []
    return [t.strip() for t in r.stdout.strip().splitlines() if t.strip()]


def _git_dirty_commits(repo_path: Path, branch: str) -> list[str]:
    """Return commit hashes in branch..HEAD (reachable from HEAD, not from branch), oldest first."""
    r = subprocess.run(
        ["git", "rev-list", "--reverse", branch + "..HEAD"],
        cwd=repo_path,
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        return []
    return [h.strip().lower() for h in r.stdout.strip().splitlines() if h.strip()]


def main() -> int:
    branch = (sys.argv[1] if len(sys.argv) > 1 else "main").strip()
    repo_path = Path(__file__).resolve().parent.parent
    if not (repo_path / ".git").exists():
        print("Error: no .git in %s" % repo_path, file=sys.stderr)
        return 1

    try:
        from pydriller import Repository
    except ImportError:
        print("Install: pip install pydriller matplotlib", file=sys.stderr)
        return 1

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("Install: pip install matplotlib", file=sys.stderr)
        return 1

    print("Loading branch '%s' (first-parent only)..." % branch, flush=True)
    try:
        hashes = _git_first_parent_commits(repo_path, branch)
    except RuntimeError as e:
        print("%s" % e, file=sys.stderr)
        return 1

    if not hashes:
        print("No commits on branch '%s'." % branch, file=sys.stderr)
        return 0

    hashes = [h.strip().lower() for h in hashes]
    hashes = [_expand_hash(repo_path, h) if len(h) != 40 else h for h in hashes]
    n_branch = len(hashes)
    print("  %d commits on branch line (for plot)" % n_branch, flush=True)

    # Traverse ALL commits in branch history (topo-order) to compute stats and cumulative
    print("Computing diff stats for all commits (this may take a while)...", flush=True)
    hash_to_cumulative: dict[str, int] = {}
    hash_to_cumulative_by_lang: dict[str, dict[str, int]] = {}
    running_total = 0
    running_by_lang: dict[str, int] = {}
    repo = Repository(
        str(repo_path),
        only_in_branch=branch,
        order="topo-order",
    )
    total_commits = 0
    for i, commit in enumerate(repo.traverse_commits()):
        total_commits = i + 1
        if total_commits % 100 == 0 or total_commits == 1:
            print("\r  %d commits" % total_commits, end="", flush=True)
        ins, dels = _commit_ins_dels(commit, exclude_paths=True)
        by_lang = _commit_ins_dels_by_lang(commit)
        h = (getattr(commit, "hash", None) or str(commit.hash)).strip().lower()
        if len(h) < 40:
            h = _expand_hash(repo_path, h)
        running_total += ins - dels
        hash_to_cumulative[h] = running_total
        for lang, (a, b) in by_lang.items():
            if lang == "Other":
                continue
            running_by_lang[lang] = running_by_lang.get(lang, 0) + (a - b)
        hash_to_cumulative_by_lang[h] = dict(running_by_lang)
    print("\r  %d commits" % total_commits, flush=True)

    # Aggregate "dirty" commits: in HEAD but not in branch (branch..HEAD)
    dirty_hashes = _git_dirty_commits(repo_path, branch)
    dirty_cumulative: int | None = None
    dirty_by_lang: dict[str, int] | None = None
    if dirty_hashes:
        dirty_hashes = [_expand_hash(repo_path, h) if len(h) != 40 else h for h in dirty_hashes]
        print("Computing diff stats for %d dirty commits (branch..HEAD)..." % len(dirty_hashes), flush=True)
        base_total = running_total
        base_by_lang = dict(running_by_lang)
        for h in dirty_hashes:
            try:
                for c in Repository(str(repo_path), single=h).traverse_commits():
                    ins, dels = _commit_ins_dels(c, exclude_paths=True)
                    base_total += ins - dels
                    for lang, (a, b) in _commit_ins_dels_by_lang(c).items():
                        base_by_lang[lang] = base_by_lang.get(lang, 0) + (a - b)
                    break
            except Exception:
                pass
        dirty_cumulative = base_total
        dirty_by_lang = base_by_lang

    print("Fetching tags for branch commits...", flush=True)
    tags_per_commit = [_git_tags_at(repo_path, h) for h in hashes]

    # Plot only first-parent branch commits, using stats/cumulative from full history
    def _get_cum(h: str) -> int:
        return hash_to_cumulative.get(h, 0)

    def _get_cum_by_lang(h: str) -> dict[str, int]:
        d = hash_to_cumulative_by_lang.get(h)
        return dict(d) if d else {}

    cumulative = [_get_cum(h) for h in hashes]
    n = n_branch
    has_dirty = dirty_cumulative is not None and dirty_by_lang is not None

    # Append dirty* point when branch tip is behind HEAD
    if has_dirty:
        cumulative = cumulative + [dirty_cumulative]
        n_plot = n + 1
    else:
        n_plot = n

    # Languages that appear (ordered): known first, then Other, then rest
    all_langs = set()
    for h in hashes:
        all_langs.update(_get_cum_by_lang(h).keys())
    if has_dirty:
        all_langs.update((dirty_by_lang or {}).keys())
    ordered_langs = [x for x in LANG_ORDER if x in all_langs and x != "Other"]
    ordered_langs += sorted(all_langs - set(LANG_ORDER) - {"Other"})

    left_langs = [x for x in ordered_langs if x == "C/C++"]
    right_langs = [x for x in ordered_langs if x != "C/C++"]

    def _series_for_lang(lang: str) -> list[int]:
        s = [_get_cum_by_lang(h).get(lang, 0) for h in hashes]
        if has_dirty and dirty_by_lang is not None:
            s.append(dirty_by_lang.get(lang, 0))
        return s

    x = list(range(n_plot))
    fig, ax_left = plt.subplots(figsize=(14, 6))
    ax_left.plot(x, cumulative, label="Cumulative total", color="black", alpha=0.9, linewidth=1.2)
    for lang in left_langs:
        ax_left.plot(x, _series_for_lang(lang), label=lang, color="#1f77b4", alpha=0.85)
    ax_left.set_xlabel("Commit index")
    ax_left.set_ylabel("Lines (cumulative)", color="black")
    ax_left.tick_params(axis="y", labelcolor="black")
    ax_left.legend(loc="upper left")
    ax_left.grid(True, alpha=0.3)

    ax_right = ax_left.twinx()
    colors = ("#2ca02c", "#ff7f0e", "#d62728", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22")
    for idx, lang in enumerate(right_langs):
        ax_right.plot(x, _series_for_lang(lang), label=lang, color=colors[idx % len(colors)], alpha=0.85)
    ax_right.set_ylabel("Lines by language (cumulative net)", color="black")
    ax_right.tick_params(axis="y", labelcolor="black")
    if right_langs:
        ax_right.legend(loc="upper right", fontsize=7)

    # Tags: all at bottom as x-axis labels, slanted to fit; append dirty* when present
    tag_indices = [i for i in range(n) if i < len(tags_per_commit) and tags_per_commit[i]]
    if tag_indices or has_dirty:
        tick_positions = list(tag_indices)
        tick_labels = [tags_per_commit[i][0] for i in tag_indices]
        if has_dirty:
            tick_positions.append(n_plot - 1)
            tick_labels.append("dirty*")
        ax_left.set_xticks(tick_positions)
        ax_left.set_xticklabels(tick_labels, fontsize=6, rotation=45, ha="right")
    else:
        step = max(1, n_plot // 20)
        tick_positions = sorted(set(list(range(0, n_plot, step)) + [n_plot - 1]))
        ax_left.set_xticks(tick_positions)
        ax_left.set_xticklabels([str(i) for i in tick_positions], fontsize=7)

    title = "Branch: %s (plot: first-parent | cumulative: all commits, excl. %s)" % (branch, ", ".join(EXCLUDE_PREFIXES))
    if has_dirty:
        title += " | dirty* = branch..HEAD"
    fig.suptitle(title)
    fig.tight_layout()
    fig.subplots_adjust(bottom=0.22)
    plt.show()
    return 0


if __name__ == "__main__":
    sys.exit(main())
