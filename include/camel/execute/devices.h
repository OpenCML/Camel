/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The devices a pipeline may use (design principles 13.4; docs/design/gpu/README.cn.md 2).
 *
 * Placement is run-time configuration: the source states where computation belongs (`#gpu`,
 * `#cpu`) and which card and how many is given to the run, so the same program moves between
 * machines unchanged. `--devices` is that configuration. This is the one place it is stored: the
 * driver sets it before anything runs, and the module that executes on a device reads it (Tiles
 * selects the CUDA ordinal of the back end from it and names the memory of that device in its
 * resource declarations).
 *
 * The text of `--devices`:
 *
 *   cpu          the pipeline runs on the host; no device is asked for. A part that needs a
 *                device anyway is a diagnostic, not a silent move to the GPU.
 *   0, 0,1, ...  the CUDA ordinals the pipeline may use, in the order written. A device named
 *                here must be there. Today a pipeline executes on one device, the first named;
 *                the others are checked and not used (multi-device execution is G7).
 *
 * Without the option there is no selection: the run prefers CUDA device 0 and runs on the host
 * when there is none, which is the behaviour programs had before `--devices`. With it a named
 * device is a requirement: a missing one is RuntimeDiag::DeviceUnavailable naming it, never a
 * fallback to the host or to another device. That is the difference between "may use a GPU" and
 * "must use device 2".
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace camel::execute {

/// What `--devices` selected. `given` separates "no option" (prefer a device, fall back to the
/// host) from an explicit selection (the named devices are requirements).
struct DeviceSelection {
    /// `--devices cpu`: the host only.
    bool cpu = false;
    /// The CUDA ordinals the pipeline may use, in the order written; empty for the host only.
    std::vector<int> cuda;
    /// Whether `--devices` was given at all.
    bool given = false;

    bool operator==(const DeviceSelection &) const = default;
};

/// Reads the value of `--devices`: `cpu`, or CUDA ordinals separated by commas. Returns nullopt
/// and sets `why` to a sentence naming what is wrong when the text is not a device list.
std::optional<DeviceSelection> parseDeviceSelection(std::string_view text, std::string &why);

/// Applies `--devices`, before anything runs. The selection stays for the rest of the process.
void selectDevices(DeviceSelection selection);

/// The selection in effect; the default (no `--devices`) is not given and names no device.
const DeviceSelection &selectedDevices();

} // namespace camel::execute