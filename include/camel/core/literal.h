// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

enum class LiteralType {
    String,
    FString,
    Integer,
    Real,
    /// A number with an `i` suffix (`4i`, `-4.5i`): the literal's text keeps the suffix.
    Imaginary,
    Boolean,
    Null,
};

class Literal {
  public:
    Literal(LiteralType type, const std::string &data) : type_(type), data_(data) {}
    ~Literal() = default;
    const std::string toString() const { return data_; }

    LiteralType type() const { return type_; }
    const std::string &data() const { return data_; }

  private:
    LiteralType type_;
    std::string data_;
};
