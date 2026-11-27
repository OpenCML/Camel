A miniature standard library for `docs.snippets_free_name_is_imported`: the example below calls a
function only `types` exports, and this page has no import of its own, so the checker has to import
it from there. The module names are the real ones because the compiler resolves an import against
the install it runs, not against this tree -- what is miniature here is the library's shape, not its
module names. The tree is checked by naming it as `--stdlib`, never as a page.