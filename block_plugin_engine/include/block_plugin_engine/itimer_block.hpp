// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "block_plugin_engine/ischeduled_block.hpp"

namespace bpe {

// Source compatibility for blocks written before BPE 1.0.
// Include ischeduled_block.hpp and inherit IScheduledBlock in new code.
using ITimerBlock [[deprecated("Use bpe::IScheduledBlock instead")]] = IScheduledBlock;

} // namespace bpe
