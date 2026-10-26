# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Derives Promo Mono NL from JetBrains Mono 2.304: the same glyphs with the contextual and
ligature features (calt, liga, dlig, clig) removed, so the code on screen shows the characters a
viewer types (`->` stays two characters). The family is renamed, because the result is a modified
version and JetBrains ships its own "JetBrains Mono NL"; the copyright and trademark notices stay
as the original has them, and OFL.txt beside this script is the original's licence.

Usage: python3 derive.py SOURCE_DIR   (the directory holding JetBrainsMono-Regular.ttf and -Medium.ttf)
"""
import os
import sys

from fontTools.ttLib import TTFont

DROPPED = {"calt", "liga", "dlig", "clig"}
FAMILY = "Promo Mono NL"
PS_FAMILY = "PromoMonoNL"
DESCRIPTION = ("Promo Mono NL is derived from JetBrains Mono 2.304 for the Camel promo film: the calt, liga, "
               "dlig and clig features are removed and the family is renamed. Licensed under the SIL Open Font "
               "License, Version 1.1.")


def drop_features(font):
    gsub = font["GSUB"].table
    records = gsub.FeatureList.FeatureRecord
    keep = [i for i, r in enumerate(records) if r.FeatureTag not in DROPPED]
    remap = {old: new for new, old in enumerate(keep)}
    gsub.FeatureList.FeatureRecord = [records[i] for i in keep]
    gsub.FeatureList.FeatureCount = len(keep)
    for script in gsub.ScriptList.ScriptRecord:
        systems = [script.Script.DefaultLangSys] + [r.LangSys for r in script.Script.LangSysRecord]
        for ls in systems:
            if ls is None:
                continue
            ls.FeatureIndex = [remap[i] for i in ls.FeatureIndex if i in remap]
            ls.FeatureCount = len(ls.FeatureIndex)
            if ls.ReqFeatureIndex != 0xFFFF:
                ls.ReqFeatureIndex = remap.get(ls.ReqFeatureIndex, 0xFFFF)


def rename(font):
    names = font["name"]
    for record in names.names:
        # 0 copyright, 7 trademark, 8 manufacturer, 9 designer, 11-14 URLs and licence: kept as they are
        if record.nameID in (1, 3, 4, 16, 18, 21):
            record.string = str(record).replace("JetBrains Mono", FAMILY)
        elif record.nameID == 6:
            record.string = str(record).replace("JetBrainsMono", PS_FAMILY)
    names.setName(DESCRIPTION, 10, 3, 1, 0x409)
    names.setName(DESCRIPTION, 10, 1, 0, 0)


def main():
    src = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    for weight in ("Regular", "Medium"):
        font = TTFont(os.path.join(src, f"JetBrainsMono-{weight}.ttf"))
        drop_features(font)
        rename(font)
        out = os.path.join(here, f"{PS_FAMILY}-{weight}.ttf")
        font.save(out)
        print(out)


if __name__ == "__main__":
    main()
