/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <cmath>
#include <openrct2-ui/guest/GuestDecisionView.h>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2-ui/windows/Windows.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Line.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ui/WindowManager.h>

namespace OpenRCT2::Ui::Windows
{
    // clang-format off
    static constexpr Widget _mainWidgets[] = {
        makeWidget({0, 0}, {0, 0}, WidgetType::viewport, WindowColour::primary),
    };
    // clang-format on

    class MainWindow final : public Window
    {
        std::optional<GuestDecisionStatus> _cohortDisplayStatus;

    public:
        void onOpen() override
        {
            gGuestDecisionOverlaySelection.reset();
            setWidgets(_mainWidgets);
            widgets[0].right = width;
            widgets[0].bottom = height;

            ViewportCreate(*this, windowPos, width, height, Focus(CoordsXYZ(0x0FFF, 0x0FFF, 0)));
            if (viewport != nullptr)
            {
                SetViewportFlags();
                viewport->rotation = 0;
            }
            gShowGridLinesRefCount = 0;
            gShowLandRightsRefCount = 0;
            gShowConstructionRightsRefCount = 0;
            WindowFootpathResetSelectedPath();
        }

        void onDraw(Drawing::RenderTarget& rt) override
        {
            ViewportRender(rt, viewport);
            if (viewport != nullptr)
                DrawDecisionOverlay(rt);
        }

        void onUpdate() override
        {
            if (gGuestDecisionOverlaySelection)
                invalidate();
            if (Config::Get().interface.guestDecisionAB && Config::Get().interface.guestDecisionCohortColours)
            {
                const auto status = GetGuestDecisionStats().status;
                if (_cohortDisplayStatus != status)
                    invalidate();
                _cohortDisplayStatus = status;
            }
            else if (_cohortDisplayStatus)
            {
                _cohortDisplayStatus.reset();
                invalidate();
            }
        }

    private:
        void DrawDecisionOverlay(Drawing::RenderTarget& rt)
        {
            using namespace Drawing;
            if (!gGuestDecisionOverlaySelection)
                return;
            const auto selection = *gGuestDecisionOverlaySelection;
            const auto* guest = getGameState().entities.getEntity<Guest>(EntityId::FromUnderlying(selection.entityId));
            if (guest == nullptr || guest->peepId != selection.guestId)
            {
                gGuestDecisionOverlaySelection.reset();
                return;
            }
            if (guest->x == kLocationNull)
                return;
            const auto* record = GetGuestDecisionRecord(selection.entityId, selection.guestId);
            if (record == nullptr)
                return;

            const auto project = [this](const CoordsXYZ& position) {
                const auto point = Translate3DTo2DWithZ(viewport->rotation, position) - viewport->viewPos;
                return viewport->pos
                    + ScreenCoordsXY{ viewport->zoom.ApplyInversedTo(point.x), viewport->zoom.ApplyInversedTo(point.y) };
            };
            const auto origin = project({ guest->x, guest->y, guest->z });
            const auto offeredColour = getColourMap(Colour::lightBlue).midLight;
            const auto acceptedColour = getColourMap(Colour::brightGreen).midLight;
            const auto targetColour = getColourMap(Colour::lightOrange).midLight;
            const auto background = getColourMap(Colour::grey).darkest;
            const auto drawDestination = [&](uint16_t destinationId, const std::string& label, PaletteIndex colour,
                                             bool solid) {
                const auto* ride = GetRide(RideId::FromUnderlying(destinationId));
                if (ride == nullptr || ride->getStation().start.isNull())
                    return;
                // Match candidate distance's station anchor, not an inferred entrance or native walking route.
                const auto destination = project(ride->getStation().getStart().toTileCentre());
                if (solid)
                    GfxDrawLine(rt, { origin, destination }, colour);
                else
                    GfxDrawDashedLine(rt, { origin, destination }, 5, colour);
                Rectangle::fill(rt, { destination - ScreenCoordsXY{ 4, 4 }, destination + ScreenCoordsXY{ 4, 4 } }, background);
                Rectangle::fill(rt, { destination - ScreenCoordsXY{ 2, 2 }, destination + ScreenCoordsXY{ 2, 2 } }, colour);
                const auto labelPosition = destination + ScreenCoordsXY{ 7, -6 };
                const auto labelWidth = std::min(240, viewport->pos.x + viewport->width - labelPosition.x - 2);
                if (labelWidth > 0 && viewport->ContainsScreen(labelPosition))
                {
                    Rectangle::fill(
                        rt, { labelPosition - ScreenCoordsXY{ 2, 1 }, labelPosition + ScreenCoordsXY{ labelWidth, 11 } },
                        getColourMap(Colour::grey).lightest);
                    DrawGuestDecisionText(rt, labelPosition, labelWidth, label);
                }
            };
            bool targetOffered = false;
            for (size_t i = 0; i < record->snapshot.candidates.size(); ++i)
            {
                const auto& candidate = record->snapshot.candidates[i];
                const bool accepted = record->choice == i
                    && (record->outcome == GuestDecisionOutcome::applied || record->outcome == GuestDecisionOutcome::unchanged);
                const bool currentTarget = guest->guestHeadingToRideId.ToUnderlying() == candidate.rideId;
                targetOffered |= currentTarget;
                auto label = std::string(1, static_cast<char>('A' + i)) + ": " + candidate.name;
                if (i < record->probabilities.size() && std::isfinite(record->probabilities[i]))
                    label += " "
                        + FormatStringID(
                                 STR_GUEST_DECISION_SCORE_PERCENT,
                                 static_cast<int32_t>(std::clamp(record->probabilities[i], 0.0f, 1.0f) * 100 + 0.5f));
                if (currentTarget)
                    label += " - " + FormatStringID(STR_GUEST_DECISION_OVERLAY_TARGET);
                drawDestination(
                    candidate.rideId, label,
                    accepted            ? acceptedColour
                        : currentTarget ? targetColour
                                        : offeredColour,
                    accepted);
            }
            if (!targetOffered && !guest->guestHeadingToRideId.IsNull())
                drawDestination(
                    guest->guestHeadingToRideId.ToUnderlying(), FormatStringID(STR_GUEST_DECISION_OVERLAY_TARGET), targetColour,
                    false);
            Rectangle::fill(rt, { origin - ScreenCoordsXY{ 3, 3 }, origin + ScreenCoordsXY{ 3, 3 } }, background);
            Rectangle::fill(rt, { origin - ScreenCoordsXY{ 1, 1 }, origin + ScreenCoordsXY{ 1, 1 } }, acceptedColour);

            const auto legend = viewport->pos + ScreenCoordsXY{ 6, viewport->height - 48 };
            const auto legendWidth = std::max(0, std::min(610, viewport->width - 12));
            if (legendWidth == 0)
                return;
            Rectangle::fill(
                rt, { legend - ScreenCoordsXY{ 3, 2 }, legend + ScreenCoordsXY{ legendWidth + 2, 26 } },
                getColourMap(Colour::grey).lightest);
            const auto age = static_cast<int32_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - record->updated).count());
            DrawGuestDecisionText(
                rt, legend, legendWidth,
                (record->cohort ? std::string(*record->cohort == GuestDecisionCohort::baseline ? "A: " : "B: ") : "")
                    + guest->getName() + " - " + FormatStringID(GuestDecisionOutcomeLabel(*record)) + " - "
                    + FormatStringID(STR_GUEST_DECISION_AGE, age, record->elapsedMilliseconds));
            DrawGuestDecisionText(rt, legend + ScreenCoordsXY{ 0, 13 }, legendWidth, STR_GUEST_DECISION_OVERLAY_LEGEND);
        }

        void SetViewportFlags()
        {
            viewport->flags.set(ViewportFlag::soundOn);
            if (Config::Get().general.invisibleRides)
                viewport->flags.set(ViewportFlag::invisibleRides);
            if (Config::Get().general.invisibleVehicles)
                viewport->flags.set(ViewportFlag::invisibleVehicles);
            if (Config::Get().general.invisibleTrees)
                viewport->flags.set(ViewportFlag::invisibleVegetation);
            if (Config::Get().general.invisibleScenery)
                viewport->flags.set(ViewportFlag::invisibleScenery);
            if (Config::Get().general.invisiblePaths)
                viewport->flags.set(ViewportFlag::invisiblePaths);
            if (Config::Get().general.invisibleSupports)
                viewport->flags.set(ViewportFlag::invisibleSupports);
        }
    };

    /**
     * Creates the main window that holds the main viewport.
     *  rct2: 0x0066B3E8
     */
    WindowBase* MainOpen()
    {
        auto* windowMgr = GetWindowManager();
        return windowMgr->Create<MainWindow>(
            WindowClass::mainWindow, { 0, 0 }, { ContextGetWidth(), ContextGetHeight() },
            { WindowFlag::stickToBack, WindowFlag::noTitleBar });
    }
} // namespace OpenRCT2::Ui::Windows
