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


def retagged_metadata(text, tags, purelib):
    """`WHEEL`'s text with this wheel's tags, keeping every line in the header block.

    The header block ends at the first blank line; anything after it is the message body, and
    a `Tag:` there is not a tag at all. Appending to the parsed lines therefore looked right in
    a text editor and was invisible to `auditwheel repair`, which reports the tags it found as
    empty, decides `No WHEEL info change needed` and leaves the old tag in place.
    """
    lines = text.splitlines()
    end = next((index for index, line in enumerate(lines) if not line.strip()), len(lines))
    # A wheel retagged by an earlier version carries a second Generator:/Tag: block *after* the
    # blank line. Those lines are not headers, nothing reads them, and a `Tag:` among them names
    # the platform the wheel was retagged away from -- so they are dropped from both halves.
    drop = ("Tag:", "Generator:", "Root-Is-Purelib:")
    headers = [line for line in lines[:end] if not line.startswith(drop)]
    headers.append("Generator: camel scripts/wheel_retag.py")
    # A wheel that names a platform is installed into the platform tree; `py3-none-any` is the
    # only form that belongs in the pure-python one, and pip reads this line to decide.
    headers.append("Root-Is-Purelib: %s" % ("true" if purelib else "false"))
    headers += ["Tag: " + tag for tag in tags]
    body = [line for line in lines[end:] if not line.startswith(drop)]
    while body and not body[-1].strip():
        body.pop()
    kept = headers + ([""] + body if body else [])
    return "\n".join(kept) + "\n"


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
                    data = retagged_metadata(
                        data.decode("utf-8"), tags, purelib=(platform_tag == "any")
                    ).encode("utf-8")
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