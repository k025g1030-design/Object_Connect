#include "../TestSupport.hpp"

#include "RetroFPS/Game/CampaignRunState.hpp"

#include <array>
#include <string>

namespace fps::tests {

void RunCampaignRunStateTests(TestContext& context) {
    CampaignRunState run;
    const std::array rooms{
        CampaignRoomDefinition{"room_01", "ROOM 1"},
        CampaignRoomDefinition{"room_02", "ROOM 2"},
    };
    std::string error;
    context.Expect(run.Initialize(rooms, error), "campaign stats initialize from unique rooms");
    context.Expect(run.EnterRoom("room_01"), "campaign marks the first room visited");
    context.Expect(run.RecordKill("room_01"), "visited room records a kill");
    context.Expect(!run.RecordKill("room_02"), "unvisited room cannot receive a kill");
    run.Fail();
    context.Expect(
        run.GetOutcome() == CampaignOutcome::PlayerDied &&
            run.FindRoom("room_01") != nullptr && run.FindRoom("room_01")->kills == 1,
        "death preserves visited room statistics");
    run.ResetRun();
    context.Expect(
        run.GetOutcome() == CampaignOutcome::InProgress &&
            run.FindRoom("room_01") != nullptr && !run.FindRoom("room_01")->visited &&
            run.FindRoom("room_01")->kills == 0,
        "new run clears outcome, visits, and kills");

    context.Expect(
        run.EnterRoom("room_01") && run.RecordKill("room_01") &&
            run.EnterRoom("room_02") && run.RecordKill("room_02") &&
            run.RecordKill("room_02"),
        "successful run records actual kills independently for every visited room");
    run.Complete();
    context.Expect(
        run.GetOutcome() == CampaignOutcome::Completed &&
            run.FindRoom("room_01") != nullptr && run.FindRoom("room_01")->kills == 1 &&
            run.FindRoom("room_02") != nullptr && run.FindRoom("room_02")->kills == 2,
        "mission completion preserves per-room Results statistics");
}

} // namespace fps::tests
