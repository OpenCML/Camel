/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

// The value of `--devices` (see camel/execute/devices.h), read and written by the driver.
#include "camel/execute/devices.h"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>

namespace camel::execute {
namespace {

/// The selection of the process. The driver sets it before the context exists (single-threaded);
/// afterwards every back end only reads it, so no lock is taken on the execution path.
DeviceSelection &selectionStorage() {
    static DeviceSelection selection;
    return selection;
}

constexpr int kMaxDeviceOrdinal = 65535;

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> items;
    while (true) {
        const size_t at = text.find(separator);
        items.push_back(text.substr(0, at));
        if (at == std::string_view::npos)
            return items;
        text.remove_prefix(at + 1);
    }
}

/// The ordinal in `text`, or -1 when it is not decimal digits in the range a device ordinal has.
int readOrdinal(std::string_view text) {
    if (text.empty())
        return -1;
    for (const char c : text)
        if (c < '0' || c > '9')
            return -1;
    int value    = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    if (r.ec != std::errc{} || r.ptr != text.data() + text.size() || value > kMaxDeviceOrdinal)
        return -1;
    return value;
}

} // namespace

std::optional<DeviceSelection> parseDeviceSelection(std::string_view text, std::string &why) {
    DeviceSelection selection;
    selection.given  = true;
    const auto items = split(text, ',');
    std::vector<int> ordinals;
    for (size_t i = 0; i < items.size(); ++i) {
        const std::string_view item = trim(items[i]);
        if (item.empty()) {
            why = "an empty entry; write cpu or a CUDA device ordinal (0 to 65535)";
            return std::nullopt;
        }
        if (item == "cpu") {
            // The host is not a device beside the CUDA ones: a pipeline either runs wholly on
            // the host or uses devices, and a list that says both is a mistake, not a fallback.
            if (items.size() != 1) {
                why = "cpu must be the only entry: the host is not a device beside the CUDA "
                      "devices";
                return std::nullopt;
            }
            selection.cpu = true;
            return selection;
        }
        const int ordinal = readOrdinal(item);
        if (ordinal < 0) {
            why = "'" + std::string(item) +
                  "' is not a device; write cpu or a CUDA device "
                  "ordinal (0 to 65535)";
            return std::nullopt;
        }
        if (std::find(ordinals.begin(), ordinals.end(), ordinal) != ordinals.end()) {
            why = "CUDA device " + std::to_string(ordinal) + " is named more than once";
            return std::nullopt;
        }
        ordinals.push_back(ordinal);
    }
    selection.cuda = std::move(ordinals);
    return selection;
}

void selectDevices(DeviceSelection selection) { selectionStorage() = std::move(selection); }

const DeviceSelection &selectedDevices() { return selectionStorage(); }

} // namespace camel::execute