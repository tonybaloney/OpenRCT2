/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <memory>
#include <string>

namespace OpenRCT2
{
    struct IGuestThoughtBackend;
}

namespace OpenRCT2::Ui
{
    std::unique_ptr<IGuestThoughtBackend> CreateLlamaGuestThoughtBackend(
        std::string runtimeDirectory, std::string modelPath, bool forceCpu = false);
}
