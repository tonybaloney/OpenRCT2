/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <algorithm>
#include <gtest/gtest.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/core/String.hpp>
#include <openrct2/entity/Guest.h>
#include <openrct2/peep/GuestNarrative.h>

using namespace OpenRCT2;

static GuestNarrativeSnapshot HealthyGuest()
{
    GuestNarrativeSnapshot snapshot;
    snapshot.happiness = 200;
    snapshot.energy = 100;
    snapshot.hunger = 200;
    snapshot.thirst = 200;
    snapshot.activity = static_cast<uint8_t>(PeepState::walking);
    return snapshot;
}

TEST(GuestNarrative, ActualParametersAndInvertedNeeds)
{
    auto snapshot = HealthyGuest();
    snapshot.happiness = 75;
    snapshot.energy = 52;
    snapshot.hunger = 8;
    const auto prompt = json_t::parse(snapshot.prompt());
    EXPECT_EQ(prompt["happiness"]["value"], 75);
    EXPECT_EQ(prompt["energy"]["value"], 52);
    EXPECT_EQ(prompt["energy"]["normal_range"], json_t({ 32, 128 }));
    EXPECT_EQ(prompt["hunger"]["value"], 8);
    EXPECT_EQ(prompt["thirst"]["value"], 200);
    EXPECT_EQ(prompt["hunger"]["direction"], "lower means hungrier");
    EXPECT_EQ(snapshot.needs(), (std::vector<std::string>{ "hungry", "tired" }));
    snapshot.hasFoodOrDrink = true;
    EXPECT_EQ(snapshot.needs(), (std::vector<std::string>{ "tired" }));
}

TEST(GuestNarrative, ThresholdsAndPriority)
{
    auto snapshot = HealthyGuest();
    snapshot.nausea = 140;
    snapshot.toilet = 160;
    snapshot.thirst = 25;
    snapshot.hunger = 10;
    EXPECT_EQ(snapshot.needs(), (std::vector<std::string>{ "sick", "toilet", "thirsty", "hungry" }));
    EXPECT_EQ(snapshot.feeling(), "nauseous");
    EXPECT_EQ(json_t::parse(snapshot.prompt())["nausea"]["feeling"], "nauseous");
    snapshot.nausea = 139;
    snapshot.toilet = 159;
    snapshot.thirst = 26;
    snapshot.hunger = 11;
    EXPECT_TRUE(snapshot.needs().empty());
}

TEST(GuestNarrative, SemanticChangesNotSmallFluctuations)
{
    auto snapshot = HealthyGuest();
    const auto key = snapshot.semanticKey();
    --snapshot.hunger;
    --snapshot.energy;
    --snapshot.happiness;
    ++snapshot.queueTime;
    EXPECT_EQ(snapshot.semanticKey(), key);
    snapshot.happiness = 179;
    EXPECT_NE(snapshot.semanticKey(), key);
    snapshot.happiness = 79;
    const auto unhappy = snapshot.semanticKey();
    snapshot.happiness = 80;
    EXPECT_NE(snapshot.semanticKey(), unhappy);
    snapshot.guestId = 42;
    EXPECT_NE(snapshot.semanticKey(), key);
}

TEST(GuestNarrative, MoneyAndQueueContext)
{
    auto snapshot = HealthyGuest();
    snapshot.cash = 123;
    EXPECT_FALSE(json_t::parse(snapshot.prompt()).contains("cash_raw_tenths"));
    EXPECT_FALSE(json_t::parse(snapshot.prompt()).contains("queue_time_raw"));
    snapshot.moneyEnabled = true;
    snapshot.queueTime = 513;
    snapshot.activity = static_cast<uint8_t>(PeepState::queuing);
    const auto prompt = json_t::parse(snapshot.prompt());
    EXPECT_EQ(prompt["cash_raw_tenths"], 123);
    EXPECT_EQ(prompt["queue_time_raw"], 513);
}

TEST(GuestNarrative, VisibleThoughtsIgnoreAgingAndPending)
{
    Guest guest{};
    guest.thoughts[0].type = PeepThoughtType::hungry;
    guest.thoughts[0].freshness = 0;
    guest.thoughts[1].type = PeepThoughtType::wasGreat;
    guest.thoughts[1].freshness = 1;
    guest.thoughts[1].rideId = RideId::GetNull();
    guest.thoughts[2].type = PeepThoughtType::none;
    const auto before = CaptureGuestNarrative(guest, false);
    EXPECT_EQ(before.thoughts, (std::vector<std::string>{ "enjoyed a ride" }));
    ++guest.thoughts[1].freshness;
    ++guest.thoughts[1].fresh_timeout;
    EXPECT_EQ(CaptureGuestNarrative(guest, false).semanticKey(), before.semanticKey());
    guest.thoughts[0].freshness = 1;
    EXPECT_NE(CaptureGuestNarrative(guest, false).semanticKey(), before.semanticKey());
}

TEST(GuestNarrative, NormalizesWhitespaceAndBoundsWords)
{
    EXPECT_EQ(CompactGuestNarrative("  \"I  need\nfood\tand a seat.\" "), "I need food and a seat.");
    std::string boundary = "one";
    for (size_t i = 1; i < kGuestNarrativeMaxWords; ++i)
        boundary += " one";
    EXPECT_EQ(CompactGuestNarrative(boundary), boundary);
    const auto result = CompactGuestNarrative(boundary + " two");
    ASSERT_TRUE(result);
    EXPECT_EQ(static_cast<size_t>(std::count(result->begin(), result->end(), ' ')), kGuestNarrativeMaxWords - 1);
    EXPECT_TRUE(result->ends_with("..."));
    EXPECT_LE(String::lengthOf(result->c_str()), kGuestNarrativeMaxCharacters);
}

TEST(GuestNarrative, CharacterAndUnicodeLimits)
{
    EXPECT_EQ(
        CompactGuestNarrative(std::string(kGuestNarrativeMaxCharacters, 'a')),
        std::string(kGuestNarrativeMaxCharacters, 'a'));
    EXPECT_FALSE(CompactGuestNarrative(std::string(kGuestNarrativeMaxCharacters + 1, 'a')));
    const auto boundary = std::string(kGuestNarrativeMaxCharacters - 1, 'a') + "\xc3\xa9";
    EXPECT_EQ(CompactGuestNarrative(boundary), boundary);
    EXPECT_FALSE(CompactGuestNarrative(boundary + "\xc3\xa9"));
    const std::string unicode = "I love caf\xc3\xa9 snacks.";
    EXPECT_EQ(CompactGuestNarrative(unicode), unicode);
    EXPECT_FALSE(CompactGuestNarrative("bad\xc0\xaf"));
    EXPECT_FALSE(CompactGuestNarrative("bad\xe0\x80\xaf"));
    EXPECT_FALSE(CompactGuestNarrative("bad\xed\xa0\x80"));
    EXPECT_FALSE(CompactGuestNarrative("bad\xf4\x90\x80\x80"));
    EXPECT_FALSE(CompactGuestNarrative("bad\xe2\x82"));
    EXPECT_FALSE(CompactGuestNarrative(
        "bad\xe2"
        "AB"));
}

TEST(GuestNarrative, RejectsFormattingAndRoleArtifacts)
{
    for (auto text :
         { "", " ", "1234", "assistant: Hello", "<|assistant|>", "{RED}Hungry", "I feel \xee\x80\x80 sick", "I feel\x01sick" })
        EXPECT_FALSE(CompactGuestNarrative(text)) << text;
}

TEST(GuestNarrative, PromptMatchesDisplayLimits)
{
    EXPECT_EQ(kGuestNarrativeMaxWords, 60u);
    EXPECT_EQ(kGuestNarrativeMaxCharacters, 360u);
    EXPECT_EQ(kGuestNarrativeMaxLines, 12);
    EXPECT_NE(GuestNarrativeSystemPrompt().find("60 words and 360 characters"), std::string_view::npos);
    EXPECT_NE(GuestNarrativeSystemPrompt().find("one short paragraph"), std::string_view::npos);
}

TEST(GuestNarrative, PreservesGroundedParagraph)
{
    auto snapshot = HealthyGuest();
    snapshot.hunger = 8;
    const std::string paragraph =
        "My stomach's growling, and it keeps stealing my attention. I could really go for a snack before thinking about "
        "anything else. A bit of food would make this walk much more pleasant.";
    EXPECT_GT(std::count(paragraph.begin(), paragraph.end(), ' '), 15);
    EXPECT_GT(String::lengthOf(paragraph.c_str()), 96u);
    EXPECT_EQ(CompactGuestNarrative(paragraph), paragraph);
    EXPECT_TRUE(IsGuestNarrativeGrounded(snapshot, paragraph));
}

TEST(GuestNarrative, CharacterTruncationIncludesEllipsis)
{
    const auto prefix = std::string(kGuestNarrativeMaxCharacters - 5, 'a');
    EXPECT_EQ(CompactGuestNarrative(prefix + " word"), prefix + " word");
    EXPECT_EQ(CompactGuestNarrative(prefix + " word extra"), prefix + "...");
}

TEST(GuestNarrative, RejectsUnrelatedAndContradictoryNeeds)
{
    auto snapshot = HealthyGuest();
    EXPECT_FALSE(IsGuestNarrativeGrounded(snapshot, "I am hungry."));
    EXPECT_FALSE(IsGuestNarrativeGrounded(snapshot, "I need a drink."));
    snapshot.hunger = 8;
    EXPECT_TRUE(IsGuestNarrativeGrounded(snapshot, "I need food."));
    EXPECT_FALSE(IsGuestNarrativeGrounded(snapshot, "This place is magical!"));
    EXPECT_FALSE(IsGuestNarrativeGrounded(snapshot, "I am not hungry."));
    EXPECT_FALSE(IsGuestNarrativeGrounded(snapshot, "I need food and water because I am thirsty."));
    snapshot.hunger = 200;
    snapshot.happiness = 50;
    EXPECT_FALSE(IsGuestNarrativeGrounded(snapshot, "This place is magical!"));
}

TEST(GuestNarrative, ParagraphNegationOnlyRejectsNeedContradictions)
{
    auto snapshot = HealthyGuest();
    snapshot.hunger = 8;
    EXPECT_TRUE(IsGuestNarrativeGrounded(snapshot, "I need food. I don't want to miss the fun."));
    EXPECT_TRUE(IsGuestNarrativeGrounded(snapshot, "I'm hungry, but I'm not sure where to go."));
    for (const auto text : { "I am not hungry.", "I no longer need food.", "I don't need to eat.",
                             "I do not feel hungry.", "My stomach isn't hungry.", "We aren't hungry." })
        EXPECT_FALSE(IsGuestNarrativeGrounded(snapshot, text)) << text;
    snapshot.happiness = 75;
    snapshot.energy = 52;
    EXPECT_FALSE(IsGuestNarrativeGrounded(snapshot, "I'm hungry, but I'm not tired."));
    snapshot.hunger = 200;
    EXPECT_TRUE(IsGuestNarrativeGrounded(snapshot, "I need a break, but I don't want to miss anything."));
    EXPECT_FALSE(IsGuestNarrativeGrounded(snapshot, "I'm not tired, so I can keep going."));
}

TEST(GuestNarrative, OldNeedsDoNotOverrideCurrentState)
{
    auto snapshot = HealthyGuest();
    snapshot.thoughts = { "hungry", "feeling sick", "enjoyed a ride" };
    EXPECT_EQ(snapshot.feeling(), "enjoyed a ride");
    snapshot.thirst = 8;
    EXPECT_EQ(snapshot.feeling(), "thirsty");
    snapshot.hunger = 8;
    snapshot.toilet = 200;
    EXPECT_EQ(snapshot.feeling(), "toilet");
    snapshot.thirst = 200;
    snapshot.hunger = 200;
    snapshot.toilet = 0;
    snapshot.happiness = 30;
    EXPECT_EQ(snapshot.feeling(), "unhappy");
}
