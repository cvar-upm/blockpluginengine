// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

namespace bpe {

enum class ExecutionResult { SUCCESS, FAILURE };

class IScheduledBlock {
public:
  virtual ~IScheduledBlock() = default;

  virtual ExecutionResult execute() = 0;
};

} // namespace bpe
