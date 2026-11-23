// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <yaml-cpp/yaml.h>

namespace bpe {

// Semantic name used by the BlockPluginEngine API.
// The current implementation uses YAML::Node, but Blocks depend on
// BlockConfig rather than on that implementation detail in their signatures.
using BlockConfig = YAML::Node;

} // namespace bpe
