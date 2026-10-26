# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

# The translation layer of the rule verifier (docs/design/rewrite/plan-verification.cn.md, S1). It
# is not a Camel module: it registers no operator and declares nothing in stdlib, so it is not a
# .cmo -- the one place it is called from is modules/rewrite, and these sources are compiled into
# that module. S3 and S4 add files beside translate.cpp (the floating theory, the licences as
# premises) and grow this list.
set(CAMEL_VERIFY_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/translate.cpp
  PARENT_SCOPE)