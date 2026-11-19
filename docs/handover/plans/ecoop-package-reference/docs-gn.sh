#!/bin/bash
# The documents of the GN package: the language reference and sections 1-12 of the design principles,
# with process remarks and links to documents outside the package removed.
set -euo pipefail
H=${H:-ae874ce04}
OUT=/work/gn-pkg/gn
cd /work/Camel
{
  printf '# GN Design Principles\n\n'
  printf 'This document states what GN fundamentally is and the rules of its semantics. It is normative: where the language reference or the implementation differs from it, this document prevails.\n\n'
  git show "$H:docs/design-principles.en.md" | sed -n '/^## 1\. /,/^## 13\. /p' | sed '$d'
} > "$OUT/docs/design-principles.md"
git show "$H:ZEN" > "$OUT/ZEN"
cd "$OUT"
DOCS=(docs/design-principles.md docs/language/*.md ZEN)
sed -i -E \
  -e '/^English \| \[/d; /^\[English\]/d' \
  -e 's/ ?\((the )?(owner|language author|author)(, revised)?,? [0-9]{4}-[0-9]{2}-[0-9]{2}[^)]*\)//g' \
  -e 's/,? ?(the )?(owner|language author|author)(\x27s ruling)?( of)?,? [0-9]{4}-[0-9]{2}-[0-9]{2}( [0-9]{2}:[0-9]{2})?//g' \
  -e 's#\]\((\.\./)+design-principles(\.en)?\.md#](../design-principles.md#g' \
  -e 's#\[([^]]*)\]\((\.\./)+[^)]*\)#\1#g' \
  -e 's#\[([^]]*)\]\([a-z0-9_/-]*\.cn\.md[^)]*\)#\1#g' \
  "${DOCS[@]}"
# links in the principles that point at other documents of the source tree become plain text
sed -i -E 's#\[([^]]*)\]\((language/[^)]*)\)#[\1](\2)#g; s#\[([^]]*)\]\(((design|technical|reference|handover)/[^)]*|[a-z0-9_-]+\.md[^)]*)\)#\1#g' docs/design-principles.md
sed -i -E \
  -e 's/OpenCML/GN/g; s/opencml/gn/g; s/\bCamel\b/GN/g; s/Camel/Gn/g; s/CAMEL/GN/g; s/camel/gn/g' \
  -e 's/\.cml\b/.gn/g; s/CML/GN/g; s/Cml/Gn/g; s/cml/gn/g' \
  "${DOCS[@]}"
echo "docs: $(cat "${DOCS[@]}" | wc -l) lines"
grep -noiE "author|owner|ruling|20[0-9]{2}-[0-9]{2}-[0-9]{2}|handover|\.cn\.md" "${DOCS[@]}" | head -20 || true
