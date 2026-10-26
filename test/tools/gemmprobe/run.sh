#!/bin/sh
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT
#
# Build and run the canonical-GEMM probe on the build host. Never on the workstation.
#
#   sh test/tools/gemmprobe/run.sh
#   sh test/tools/gemmprobe/run.sh --threads 1,8,16 --reps 7
#
# The translation unit and the real worker pool (kernels/parallel.cpp) are compiled together with
# the flags the build gives the AVX2 unit: -march=native -mavx2 -mfma, the project's
# -ffp-contract=off, and C++23, which parallel.cpp uses. OpenBLAS is loaded at run time with
# dlopen (the host's libopenblas.so has gfortran symbols the link line would have to resolve), so
# a host without it still runs the probe and simply prints no BLAS column. The binary is put in
# /tmp so a run leaves the checkout alone.

set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
cxx=${CXX:-$HOME/camel-work/bin/clang++}
out=${OUT:-/tmp/gemm_probe/gemm_probe}

mkdir -p "$(dirname "$out")"
"$cxx" -O3 -DNDEBUG -std=gnu++23 -march=native -mavx2 -mfma -ffp-contract=off \
    -I "$root/modules/tensor/kernels" -I "$root/modules/tensor" -I "$root/include" \
    "$here/gemm_probe.cpp" "$root/modules/tensor/kernels/parallel.cpp" \
    -o "$out" -lpthread

exec taskset -c "${CPU:-3}" "$out" "$@"