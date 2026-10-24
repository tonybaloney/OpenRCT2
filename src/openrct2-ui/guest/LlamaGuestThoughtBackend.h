/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace OpenRCT2
{
    struct IGuestThoughtBackend;
    struct IGuestDecisionBackend;
} // namespace OpenRCT2

namespace OpenRCT2::Ui
{
    struct GuestThoughtModel
    {
        std::string label;
        std::string path;
        uint64_t bytes{};
        bool instruct{};
        bool available{};
        bool allowCpuFallback{};
        bool firstPersonPrefill{};
    };

    struct GuestThoughtBackendOptions
    {
        bool forceCpu{};
        bool instruct = true;
        bool allowCpuFallback = true;
        uint64_t modelBytes{};
        bool firstPersonPrefill{};
    };

    struct GuestDecisionModel
    {
        std::string id;
        std::string label;
        bool available{};
    };

    std::vector<GuestDecisionModel> LoadGuestDecisionModels(const std::string& directory);
    std::vector<GuestThoughtModel> LoadGuestThoughtModels(const std::string& modelDirectory);
    std::unique_ptr<IGuestThoughtBackend> CreateLlamaGuestThoughtBackend(
        std::string runtimeDirectory, std::string modelPath, GuestThoughtBackendOptions options = {});
    std::unique_ptr<IGuestDecisionBackend> CreateLlamaGuestDecisionBackend(
        std::string runtimeDirectory, std::string modelDirectory);
} // namespace OpenRCT2::Ui
