// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include "detail/base_fair_scheduler.hpp"
#include <pluginlib/class_list_macros.hpp>
// The production specialization retains its original pluginlib class name.
PLUGINLIB_EXPORT_CLASS(bpe::BPE_FairScheduler, bpe::IBlockScheduler)
