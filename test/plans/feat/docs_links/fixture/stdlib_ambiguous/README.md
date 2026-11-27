A miniature standard library for `docs.snippets_two_modules_same_name_is_refused`: two modules
export `to_bytes`, as `array` and `tensor` do in the tree's own library, and the example below calls
it without saying which one it means. The checker reports the two owners instead of choosing one.
The tree is checked by naming it as `--stdlib`, never as a page.