# NN Autograd Architecture

`nn` has no autograd of its own: differentiation is the graph rewrite in `modules/autodiff` (`grad` / `with_grad`), described in [26_autodiff_as_graph_rewrite.md](26_autodiff_as_graph_rewrite.md) and design principles 9.9.
