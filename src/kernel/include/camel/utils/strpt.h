// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <ctime>
#include <string>

bool myStrptime(const std::string &datetime, const std::string &format, std::tm &out_tm);
