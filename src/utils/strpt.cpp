/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Sep. 16, 2025
 * Updated: Sep. 16, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "strpt.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

struct FormatItem {
    std::string token;
    int *target;
};

bool myStrptime(const std::string &datetime, const std::string &format, std::tm &out_tm) {
#ifdef _WIN32
    std::unordered_map<std::string, FormatItem> mapping;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

    mapping["%Y"] = {"%d", &year};   // year
    mapping["%m"] = {"%d", &month};  // month
    mapping["%d"] = {"%d", &day};    // day
    mapping["%H"] = {"%d", &hour};   // hour
    mapping["%M"] = {"%d", &minute}; // minute
    mapping["%S"] = {"%d", &second}; // second

    std::string sscanf_format;
    std::vector<int *> field_order;

    for (size_t i = 0; i < format.size();) {
        if (format[i] == '%' && i + 1 < format.size()) {
            std::string token = format.substr(i, 2);
            if (mapping.count(token)) {
                sscanf_format += mapping[token].token;
                field_order.push_back(mapping[token].target);
                i += 2;
                continue;
            }
        }

        sscanf_format += format[i];
        i++;
    }

    int matched = 0;
    const char *c_datetime = datetime.c_str();
    const char *c_format = sscanf_format.c_str();

    // 使用 sscanf_s 替代 sscanf，注意：%d 不需要额外的 buffer size
    switch (field_order.size()) {
    case 6:
        matched = sscanf_s(
            c_datetime,
            c_format,
            field_order[0],
            field_order[1],
            field_order[2],
            field_order[3],
            field_order[4],
            field_order[5]);
        break;
    case 5:
        matched = sscanf_s(
            c_datetime,
            c_format,
            field_order[0],
            field_order[1],
            field_order[2],
            field_order[3],
            field_order[4]);
        break;
    case 4:
        matched = sscanf_s(
            c_datetime,
            c_format,
            field_order[0],
            field_order[1],
            field_order[2],
            field_order[3]);
        break;
    case 3:
        matched = sscanf_s(c_datetime, c_format, field_order[0], field_order[1], field_order[2]);
        break;
    case 2:
        matched = sscanf_s(c_datetime, c_format, field_order[0], field_order[1]);
        break;
    case 1:
        matched = sscanf_s(c_datetime, c_format, field_order[0]);
        break;
    default:
        return false;
    }

    if (matched != static_cast<int>(field_order.size()))
        return false;

    std::memset(&out_tm, 0, sizeof(std::tm));
    out_tm.tm_year = year ? year - 1900 : 0;
    out_tm.tm_mon = month ? month - 1 : 0;
    out_tm.tm_mday = day;
    out_tm.tm_hour = hour;
    out_tm.tm_min = minute;
    out_tm.tm_sec = second;

    return true;

#else
    std::memset(&out_tm, 0, sizeof(std::tm));
    char *ret = strptime(datetime.c_str(), format.c_str(), &out_tm);
    return ret != nullptr;
#endif
}