# I2 — one training step of a diffusion model

The showcase design's I2 is "diffusion training: DDPM, a UNet, CIFAR-10 or 64×64 faces"
(`docs/design/showcase/README.cn.md`), and the GPU design makes a diffusion training step one of
its acceptance benchmarks. This directory is the **CPU precursor** of that example: the same
training objective, on a model small enough that three steps run in well under a second, so that
the semantics can be pinned before anything is said about a device.

`step.cml` is the whole example. It is a DDPM training step (Ho et al. 2020, the simplified
objective), on one 4×4 image, with a two-layer denoiser written as a parameter tree.

## The step, term by term

| what the code does | the formula it is |
|---|---|
| `weights(t)` returns `(sqrt(alpha_bar_t), sqrt(1 - alpha_bar_t))` | `alpha_bar_t = prod over s <= t of (1 - beta_s)`, the schedule; `beta = 0.02, 0.05, 0.10, 0.20` for the four timesteps |
| `x_t = x0 * keep + eps * add` | the forward process `x_t = sqrt(alpha_bar_t)·x_0 + sqrt(1 - alpha_bar_t)·eps`, `eps ~ N(0, I)` |
| `predicted_noise<m>(x)` | the network `eps_hat = denoiser(x_t)`: `relu(x @ w1 + b1) @ w2 + b2`, 16 → 8 → 16 |
| `loss<m>(x_t, eps)` | the simplified objective, `mean over all elements of (eps_hat - eps)²` |
| `adam(m, gradient, opt, lr)` | one Adam step of the parameters against that loss |

The parameters are a struct of four tensors, so `with_grad` returns a gradient of the same type and
`adam` consumes it without the program naming a leaf — the same shape as `showcase/c2-mlp-mnist`.

## Running it

```
camel showcase/i2-diffusion-step/step.cml --arg seed=7 --arg steps=3 --arg lr=0.02 --arg t=1
camel showcase/i2-diffusion-step/step.cml --arg seed=7 --arg steps=3 --arg lr=0.02 --arg t=1 std::prl
```

Both print the same five lines:

```
i2 config seed=7 steps=3 lr=0.02 t=1 bar=0.93100 image=4x4 batch=4
i2 step=0 loss=1.202875
i2 step=1 loss=1.103411
i2 step=2 loss=1.020298
i2 trained t=1 loss=0.944348
```

The loss falls from step to step and the reading on the trained parameters falls below the last
step's; `test/plans/feat/showcase_i2.plan.toml` asserts exactly that, and pins the five lines, so a
step that mixed up the forward process, the objective or the update goes red instead of printing a
different curve.

Every number is a function of `--arg seed`: the starting weights and the noise both come from the
counter-based generator in `random` (`key`, `fold`, `normal`), never from a clock, so two runs with
the same arguments are byte-identical and the loss is an assertion rather than a look.

**Every step of a run trains on the same timestep and the same noise** — `--arg t=0..3` walks the
schedule, and the three lines are one curve only because nothing but the parameters changes between
them. A curve whose steps each drew their own noise would mix the noise level into the reading.

**The descent is the update's doing, not the program's.** With `--arg lr=0` the same three lines
read `1.202875 / 1.202875 / 1.202875` — flat — so the assertion the plan makes ("the loss falls
from step to step") goes red the moment the update stops moving the parameters. An assertion that
cannot be made to fail is not worth pinning; this one can.

## How `std::prl` cuts it (`segments=true`)

`std::prl segments=true` prints the plan the scheduler built instead of running it, which is the
honest way to answer "where is the parallelism here":

```
$ camel showcase/i2-diffusion-step/step.cml --arg seed=7 --arg steps=3 --arg lr=0.02 --arg t=1 \
    "std::prl segments=true"
[taskflow] segments 'main': no segments, evaluated node by node: it has a loop
[taskflow] segments 'predicted_noise': 1 segment(s), 0 chain(s), 5 operator(s)
[taskflow] segments 'predicted_noise' segment 0 (top): 9 item(s), 0 chain(s), inline
[taskflow] segments 'predicted_noise' oper tensor:matmul #3: segment 0, inline
[taskflow] segments 'alpha_bar': 3 segment(s), 0 chain(s), 4 operator(s)
[taskflow] segments 'alpha_bar' segment 1 (arm of #6): 2 item(s), 0 chain(s), inline, after segment 0 item 2
[taskflow] segments 'loss': 1 segment(s), 0 chain(s), 7 operator(s)
```

Read line by line, it says three things:

- **`main` has no segments at all, and says why**: the training loop makes the scheduler evaluate the
  graph node by node (`it has a loop` — the plan's own reason, printed because the reason is kept on
  the plan). A loop is a chain of rounds, not a tree of independent work; there is nothing to cut.
- **each function is one segment, one chain, `inline`**: `predicted_noise` is five operators in a
  row (`matmul, add, relu, matmul, add`), `loss` is seven, `weights` is three. Those are the step's
  formulas from the table above, in one total order — a network's layers are sequential by
  definition.
- **`alpha_bar` is the one place with more than one segment**: three segments, the arms of the
  if/else chain, each hanging off the previous one (`arm of #6 … after segment 0 item 2`).

So the answer to "where is the parallelism" is: **in this example it is inside the operators, not
between tasks** — the matmul and relu kernels of `tensor` are where the threads are, and the task
graph has nothing independent to schedule. That is also why the two back ends print identical
numbers: the scheduler has no freedom to reorder anything. For a plan that *does* cut chains (a
branch arm cut into eleven chains with thirteen edges between them), see
`docs/handover/reports/prl-two-reds-and-segments.md`.

## What is not here, and why

- one timestep per run instead of a random one per sample: a demo's curve has to be comparable, and
  the random draw is where a real trainer differs from this one first;
- one 4×4 image instead of CIFAR-10 or 64×64 faces, and a two-layer network instead of a UNet: the
  training *step* is the subject, and a UNet would add parameters, not semantics;
- no sampler, no EMA, no learning-rate schedule: this is the training half of I2 only.

## Why this step is not on a GPU yet

The dispatch ruling is explicit (`docs/handover/reviews/questions-2026-10-29.md`): *"H237 杀手级示例、
H13 官方示例（D4 MCTS、扩散模型训练步）：GPU 那一路能跑之后接上"* — the diffusion training step joins
once the GPU path runs, and the GPU design lists it as an acceptance benchmark for exactly that
reason. So the order is: this CPU step first, the device version after it. A step whose numbers are
not pinned cannot tell a device's arithmetic apart from its own bug, which is why the CPU version is
the prerequisite rather than a detour — correct semantics first, then the device.

## Files

```
showcase/i2-diffusion-step/
  step.cml                   the Camel program (the whole example)
  README.md                  this file
  README.cn.md               Chinese README
```

The plan that runs it: `test/plans/feat/showcase_i2.plan.toml` — the default back end and
`std::prl`, each printing the same numbers.

## The device half

`step_gpu.cml` runs the denoiser's forward pass on the GPU and prints how far the device's answer is
from the host's on the same values:

```
$ camel showcase/i2-diffusion-step/step_gpu.cml --devices 5
i2gpu device=NVIDIA GeForce RTX 3090 (sm_86)
i2gpu gap=0.0
```

`gap` is the sum of squared differences over every element, so it is zero exactly when the device
and the host agree. It is the one assertion a device run can make without pinning a machine: no
duration, no ordinal and no device name is asserted anywhere.

The pass it needs is `tiles`, which translates the function graph -- `relu(x @ w1 + b1) @ w2 + b2` --
to a kernel. That expression is inside the subset the module supports today: contiguous f32 tensors,
broadcasting arithmetic, `relu`, and rank-2 `matmul`. The parameters travel as one aggregate because
`run_preferred_resident` takes one argument and reads a struct of tensors by field.

The schedule, the noise draw, the loss and the Adam update stay on the host: the loop carries the
optimizer state, which the device path does not have yet. So this is the device half of the step's
*arithmetic*, not a device half of the training loop.

**The ordinal is the machine's.** `--devices 5` names a card with free memory on the host this was
written on; on a host with one free GPU, drop it. `nvidia-smi` says which one is free -- a card held
by another process fails with `CUDA_ERROR_OUT_OF_MEMORY`, which is the environment, not the example.

**The test does not do what the command above does**, and the difference is deliberate: the plan entry
sets `cuda_free_device = true` and passes `--devices 0`, which names the one card the child can see
whichever ordinal the host gave it. The runner picks a card with room before the run starts, so the
entry reads the same on a host whose device 0 is busy. An ordinal written into a plan is the second
thing to break on someone else's machine -- a red that depends on what another process is running is
not a test, it is news.
