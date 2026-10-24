/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OpenRCT2
{
    constexpr size_t kGuestNarrativeMaxWords = 60;
    constexpr size_t kGuestNarrativeMaxCharacters = 360;
    constexpr int32_t kGuestNarrativeMaxLines = 12;

    struct Guest;

    struct GuestNarrativeSnapshot
    {
        uint32_t guestId{};
        uint16_t entityId{};
        uint8_t happiness{}, energy{}, hunger{}, thirst{}, nausea{}, toilet{};
        uint8_t activity{};
        uint16_t queueTime{};
        bool hasFoodOrDrink{}, leavingPark{}, moneyEnabled{};
        int64_t cash{};
        std::string ride;
        std::vector<std::string> thoughts;

        std::vector<std::string> needs() const;
        std::string feeling() const;
        std::string prompt() const;
        std::string semanticKey() const;
    };

    GuestNarrativeSnapshot CaptureGuestNarrative(const Guest& guest, bool moneyEnabled);
    std::string_view GuestNarrativeSystemPrompt();
    std::optional<std::string> CompactGuestNarrative(std::string_view text);
    bool IsGuestNarrativeGrounded(const GuestNarrativeSnapshot& snapshot, std::string_view text);
} // namespace OpenRCT2
