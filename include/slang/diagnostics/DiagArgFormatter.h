//------------------------------------------------------------------------------
//! @file DiagArgFormatter.h
//! @brief Interface for formatting custom diagnostic argument types
//
// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <any>

namespace slang {

class Diagnostic;

class DiagArgFormatter {
public:
    virtual ~DiagArgFormatter() {}

    virtual void startMessage(const Diagnostic&) {}
    virtual std::string format(const std::any& arg) = 0;
};

} // namespace slang
