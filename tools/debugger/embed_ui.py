#!/usr/bin/env python3
# Reads ui/index.html and generates ui_html.h with embedded string for C++.
# Usage: embed_ui.py <path/to/index.html> <path/to/ui_html.h>

import os
import sys

def main():
    if len(sys.argv) != 3:
        sys.stderr.write("Usage: embed_ui.py <index.html> <ui_html.h>\n")
        sys.exit(1)
    path_in = sys.argv[1]
    path_out = sys.argv[2]
    os.makedirs(os.path.dirname(os.path.abspath(path_out)), exist_ok=True)
    with open(path_in, "r", encoding="utf-8") as f:
        content = f.read()
    # Use a delimiter unlikely to appear in HTML/JS
    delim = "CAMEL_WEBUI"
    out = f'''/**
 * Generated from ui/index.html - do not edit by hand.
 */
#pragma once

const char *const kWebUIHtml = R"{delim}(
{content}
){delim}";
'''
    with open(path_out, "w", encoding="utf-8") as f:
        f.write(out)

if __name__ == "__main__":
    main()
