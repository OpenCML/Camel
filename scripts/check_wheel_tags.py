# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Fail a wheel whose platform tag the index will not accept, or that pip cannot place.

PyPI refused the whole 0.2.31 upload on the first file (5.37):

    400 Binary wheel 'camel_lang-0.2.31-cp310-cp310-linux_x86_64.whl' has an unsupported
    platform tag 'linux_x86_64'

The tag was `linux_x86_64` because `pypi-rollup.js` took the *first* `packaging.tags.sys_tags()`
entry, and a modern `packaging` yields the build machine's own tag there, before the
manylinux-compatible ones. A wheel carrying native binaries has to name the platform at the
level that platform's policy defines:

    Linux    manylinux_<glibc>_<arch> / musllinux_<musl>_<arch>   (`auditwheel repair`)
    macOS    macosx_<deployment target>_<arch>                    (`delocate`)
    Windows  win_amd64 / win_arm64 / win32

`*-any.whl` is refused as well: this distribution is a native build by definition.

This runs in the build step, so a wrong tag fails the job that produced it instead of being
rejected by the index after the whole matrix has been built and downloaded.

    python3 scripts/check_wheel_tags.py dist/pypi/*.whl
    python3 scripts/check_wheel_tags.py --expect manylinux_2_28_x86_64 dist/pypi/*.whl
"""

import argparse
import sys
import zipfile

# Platform tags a native wheel may carry. The value is what the tag has to start with.
ALLOWED = ("manylinux_", "musllinux_", "macosx_", "win_")
PYTHON_TAGS = ("py3", "py2.py3")
ABI_TAGS = ("none", "abi3")


def parse_wheel_name(path):
    """`name-version-pythontag-abitag-platformtag.whl` -> the last three fields."""
    stem = path.rsplit("/", 1)[-1].rsplit("\\", 1)[-1]
    if not stem.endswith(".whl"):
        raise ValueError("not a wheel: %s" % stem)
    parts = stem[: -len(".whl")].split("-")
    if len(parts) < 5:
        raise ValueError("not a wheel name (name-version-python-abi-platform): %s" % stem)
    return parts[-3], parts[-2], parts[-1]


def wheel_wheel_tags(path):
    """The `Tag:` lines in `*.dist-info/WHEEL` inside the archive."""
    tags = []
    with zipfile.ZipFile(path) as archive:
        for item in archive.namelist():
            if item.count("/") == 1 and item.endswith(".dist-info/WHEEL"):
                for line in archive.read(item).decode("utf-8").splitlines():
                    if line.startswith("Tag:"):
                        tags.append(line[len("Tag:") :].strip())
    return tags


def platform_ok(platform_tag):
    return platform_tag.startswith(ALLOWED)


def check(path, expect):
    """Return a list of problems; empty means the wheel's tags are publishable."""
    problems = []
    try:
        python_tag, abi_tag, platform_tag = parse_wheel_name(path)
    except ValueError as error:
        return [str(error)]

    if not platform_ok(platform_tag):
        problems.append(
            "platform tag %r is not one of manylinux_*/musllinux_*/macosx_*/win_* "
            "(PyPI rejects it; see this file's docstring)" % platform_tag
        )
    if expect and platform_tag != expect:
        problems.append("platform tag %r is not the expected %r" % (platform_tag, expect))

    # A tag that says `any` while the wheel carries a native binary, or a python tag that
    # claims an interpreter the ABI does not, is a malformed tag as well as a policy miss.
    if python_tag not in PYTHON_TAGS and abi_tag not in ABI_TAGS:
        if not python_tag.startswith("cp"):
            problems.append("python tag %r is neither py3 nor a cp* interpreter" % python_tag)

    inner = wheel_wheel_tags(path)
    if not inner:
        problems.append("no `Tag:` line in *.dist-info/WHEEL")
    for tag in inner:
        fields = tag.split("-")
        if len(fields) != 3:
            problems.append("malformed Tag: %r" % tag)
            continue
        if fields[2] != platform_tag:
            problems.append(
                "Tag: %r disagrees with the file name's platform tag %r; pip reads the name "
                "and the index reads both" % (tag, platform_tag)
            )
        if not platform_ok(fields[2]):
            problems.append("Tag: %r carries an unpublishable platform tag" % tag)
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("wheels", nargs="+")
    parser.add_argument(
        "--expect",
        default="",
        help="the platform tag this job is supposed to produce (e.g. manylinux_2_28_x86_64)",
    )
    args = parser.parse_args()

    failed = False
    for path in args.wheels:
        problems = check(path, args.expect)
        if problems:
            failed = True
            for problem in problems:
                print("::error file=%s::%s" % (path, problem))
                print("FAIL %s: %s" % (path, problem), file=sys.stderr)
        else:
            print("ok   %s" % path)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())