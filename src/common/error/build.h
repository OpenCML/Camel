/**
 * Copyright (c) 2024 Beijing Jiaotong University
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 28, 2024
 * Updated: Oct. 6, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <antlr4-runtime.h>
#include <exception>
#include <string>

extern std::string tmpMessage;

class BuildException : public std::exception {
  private:
    std::string message;
    size_t line;
    size_t column;

  public:
    BuildException(const std::string &msg, antlr4::Token *token) {
        line = token->getLine();
        column = token->getCharPositionInLine();
        message = msg;
    }
    const char *what() const noexcept override {
        tmpMessage = "error: line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message;
        return tmpMessage.c_str();
    }

    const char *json() const noexcept {
        tmpMessage = "{"
                     "\"type\": \"error\", "
                     "\"line\": " +
                     std::to_string(line) +
                     ", "
                     "\"column\": " +
                     std::to_string(column) +
                     ", "
                     "\"message\": \"" +
                     message +
                     "\""
                     "}";
        return tmpMessage.c_str();
    }
};

class BuildWarning {
  private:
    std::string message;
    size_t line;
    size_t column;

  public:
    BuildWarning(const std::string &msg, antlr4::Token *token) {
        line = token->getLine();
        column = token->getCharPositionInLine();
        message = msg;
    }
    const char *what() const noexcept {
        tmpMessage = "warn: line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message;
        return tmpMessage.c_str();
    }

    const char *json() const noexcept {
        tmpMessage = "{"
                     "\"type\": \"warn\", "
                     "\"line\": " +
                     std::to_string(line) +
                     ", "
                     "\"column\": " +
                     std::to_string(column) +
                     ", "
                     "\"message\": \"" +
                     message +
                     "\""
                     "}";
        return tmpMessage.c_str();
    }
};
