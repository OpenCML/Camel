# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Retag a wheel: the platform tag the file name carries, and the `Tag:` lines inside it.

`python -m wheel tags` did this until wheel 0.45 dropped its command line, and the `wheel`
distribution comes from the package index -- which a machine that builds a local wheel to look at
it before publishing (no egress) cannot reach. The work itself is stdlib work: a wheel is a zip
file, and its tags live in the file name and in `*.dist-info/WHEEL`, so `zipfile` is enough.

    python3 scripts/wheel_retag.py dist/camel_lang-0.2.30-py3-none-any.whl cp310 cp310 linux_x86_64

The wheel is rewritten in place and renamed; the printed name is the retagged file.
"""

import os
import sys
import zipfile


def retag(wheel, python_tag, abi_tag, platform_tag):
    """Rewrite `wheel`'s tags and rename it; returns the new path."""
    tags = ["%s-%s-%s" % (python_tag, abi_tag, platform_tag)]
    directory = os.path.dirname(os.path.abspath(wheel))
    rewritten = wheel + ".retag"

    with zipfile.ZipFile(wheel) as source:
        with zipfile.ZipFile(rewritten, "w", zipfile.ZIP_DEFLATED) as target:
            for item in source.infolist():
                data = source.read(item.filename)
                if item.filename.count("/") == 1 and item.filename.endswith(".dist-info/WHEEL"):
                    lines = [
                        line
                        for line in data.decode("utf-8").splitlines()
                        if not line.startswith("Tag:") and not line.startswith("Generator:")
                    ]
                    lines.append("Generator: camel scripts/wheel_retag.py")
                    lines += ["Tag: " + tag for tag in tags]
                    data = ("\n".join(lines) + "\n").encode("utf-8")
                target.writestr(item, data)

    # The name is where pip reads the tags from: name-version-python-abi-platform.whl.
    parts = os.path.basename(wheel)[: -len(".whl")].split("-")
    if len(parts) < 5:
        raise SystemExit("not a wheel name: %s" % os.path.basename(wheel))
    parts[-3:] = [python_tag, abi_tag, platform_tag]
    renamed = os.path.join(directory, "-".join(parts) + ".whl")

    os.replace(rewritten, wheel)
    os.replace(wheel, renamed)
    return renamed


def main():
    if len(sys.argv) != 5:
        raise SystemExit(__doc__.strip().splitlines()[-3].strip())
    print(retag(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]))


if __name__ == "__main__":
    main()