# The twenty-eight reds were one operator declaring nothing

develop at 5d0acb240, six plans run separately:

    compile  total: 2630, pass: 2613, fail:  0, skip: 0, known failures: 17
    linear   total:  869, pass:  858, fail:  6, skip: 2, known failures:  3
    opt      total: 1603, pass: 1581, fail: 22, skip: 0, known failures:  0
    parse    total:  121, pass:  121, fail:  0
    para     total:  104, pass:  104, fail:  0
    tools    total:   70, pass:   70, fail:  0

    total 5397, pass 5347, fail 28, skip 2, known 20

## Why twenty-eight and not six

The commit checks that a draft has to pass were asked only by a debug build. Landing them in
release made them ask, and twenty-eight cases answered no. Both families have one cause, and it
is not in an optimizer:

**A node that only passes a function through declares no resources, so the analysis reads it as
writing every resource.** ` utodiff:with_grad ` takes a function value and returns a function
value; its kernel is ` return gradFunction(...) `. It runs no body. It writes nothing. But it is
declared ` pure=false ` and never declares a resource set, which the analysis reads as "touches
anything", which meets ` `:var/after` ` reading a cell, which makes a pair with no dependency
between them. Twenty-eight cases met that pair.

## The criterion is the compiler's own sentence

The obvious declaration -- not pure, empty resource set -- is rejected by the compiler, and the
rejection states the rule being applied:

    The semantics declared for the operator 'autodiff:grad' are rejected: resources: an
    operator that is not pure touches, allocates or affects something (declare it pure instead).

So the language already says what this node is. It was the implementation that did not say it.
Declaring these four transforms pure with an empty resource set -- the shape ` stop_gradient `
already has in the same file -- makes every graph accepted, keeps every backend's output
` plain call: value 1.0 cnt 1 | with_grad: value 2.0 cnt 2 | with_grad: value 3.0 cnt 3 `,
and touches no check.

## What this means about the twenty-eight

They were never a regression and they were never ` std::opt `'s doing. Reading the front end's
own graph -- before any pass ran -- with every node treated as added gives the identical refusal.
` std::opt ` copied the pair faithfully; a rewrite would have hidden a front-end reading behind an
order edge that the language does not require.

That distinction is the whole value of the reading: **a pair with no dependency is not a pair that
is missing an edge.**

## The reds that are not this

` linear `'s six and ` opt `'s twenty-two are the same cause, but ` opt `'s twenty-two split
into eight cases with one cause (two opaque calls with no edge after inlining), eight with another
(folding moves an arm's write into the body without leaving an edge), and six that were only ever
named. Those two groups are separate defects that the same refusal was hiding.
