/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <openrct2/drawing/ColourMap.h>
#include <openrct2/drawing/Rectangle.h>
#include <openrct2/drawing/Text.h>
#include <openrct2/localisation/Formatting.h>
#include <openrct2/localisation/StringIds.h>
#include <openrct2/peep/GuestDecision.h>

namespace OpenRCT2::Ui
{
    inline void DrawGuestDecisionText(Drawing::RenderTarget& rt, ScreenCoordsXY pos, int32_t width, std::string_view text)
    {
        drawText(rt, pos, "{BLACK}");
        drawTextEllipsised(rt, pos, width, text, { Drawing::kColourNull, TextPaintFlag::noFormatting });
    }

    inline void DrawGuestDecisionText(Drawing::RenderTarget& rt, ScreenCoordsXY pos, int32_t width, StringId format)
    {
        DrawGuestDecisionText(rt, pos, width, FormatStringID(format));
    }

    inline void DrawGuestDecisionHint(Drawing::RenderTarget& rt, ScreenCoordsXY pos, int32_t width, StringId format)
    {
        drawText(rt, pos, "{BLACK}");
        drawTextWrapped(rt, pos, width, format, { Drawing::kColourNull });
    }

    inline StringId GuestDecisionStatusLabel(GuestDecisionStatus status)
    {
        switch (status)
        {
            case GuestDecisionStatus::loading:
                return STR_GUEST_DECISIONS_LOADING;
            case GuestDecisionStatus::ready:
                return STR_GUEST_DECISIONS_ACTIVE;
            case GuestDecisionStatus::unavailable:
                return STR_GUEST_DECISIONS_UNAVAILABLE;
            case GuestDecisionStatus::suspended:
                return STR_GUEST_DECISIONS_SUSPENDED;
            default:
                return STR_GUEST_DECISIONS_DISABLED;
        }
    }

    inline StringId GuestDecisionOutcomeLabel(const GuestDecisionRecord& record)
    {
        switch (record.outcome)
        {
            case GuestDecisionOutcome::pending:
            {
                const auto status = GetGuestDecisionStats().status;
                return status == GuestDecisionStatus::loading || status == GuestDecisionStatus::ready
                    ? STR_GUEST_DECISION_PENDING
                    : GuestDecisionStatusLabel(status);
            }
            case GuestDecisionOutcome::applied:
                return STR_GUEST_DECISION_APPLIED;
            case GuestDecisionOutcome::unchanged:
                return STR_GUEST_DECISION_UNCHANGED;
            case GuestDecisionOutcome::lowConfidence:
                return STR_GUEST_DECISION_LOW_CONFIDENCE;
            case GuestDecisionOutcome::stale:
                return STR_GUEST_DECISION_STALE;
        }
        return STR_GUEST_DECISION_NO_RECORD;
    }

    inline void DrawGuestDecisionScore(Drawing::RenderTarget& rt, ScreenCoordsXY pos, int32_t width, float score, bool accepted)
    {
        using namespace Drawing;
        Rectangle::fill(rt, { pos, pos + ScreenCoordsXY{ width - 1, 4 } }, getColourMap(Colour::grey).midDark);
        const auto filled = static_cast<int32_t>(score * width);
        if (filled > 0)
            Rectangle::fill(
                rt, { pos, pos + ScreenCoordsXY{ filled - 1, 4 } },
                getColourMap(accepted ? Colour::brightGreen : Colour::lightBlue).midLight);
    }
} // namespace OpenRCT2::Ui
