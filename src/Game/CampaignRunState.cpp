#include "RetroFPS/Game/CampaignRunState.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace fps {

bool CampaignRunState::Initialize(
    const std::span<const CampaignRoomDefinition> rooms,
    std::string& error) {
    error.clear();
    if (rooms.empty()) {
        error = "Campaign run state requires at least one room.";
        return false;
    }

    std::unordered_set<std::string> identifiers;
    std::vector<CampaignRoomStats> next;
    next.reserve(rooms.size());
    for (const CampaignRoomDefinition& room : rooms) {
        if (room.levelId.empty() || room.levelName.empty()) {
            error = "Campaign room IDs and names must be non-empty.";
            return false;
        }
        if (!identifiers.insert(room.levelId).second) {
            error = "Campaign room IDs must be unique: " + room.levelId;
            return false;
        }
        next.push_back({room.levelId, room.levelName, 0, false});
    }
    rooms_ = std::move(next);
    outcome_ = CampaignOutcome::InProgress;
    return true;
}

void CampaignRunState::ResetRun() noexcept {
    for (CampaignRoomStats& room : rooms_) {
        room.kills = 0;
        room.visited = false;
    }
    outcome_ = CampaignOutcome::InProgress;
}

bool CampaignRunState::EnterRoom(const std::string_view levelId) noexcept {
    const auto found = std::ranges::find_if(rooms_, [levelId](const CampaignRoomStats& room) {
        return room.levelId == levelId;
    });
    if (found == rooms_.end()) {
        return false;
    }
    found->visited = true;
    return true;
}

bool CampaignRunState::RecordKill(const std::string_view levelId) noexcept {
    const auto found = std::ranges::find_if(rooms_, [levelId](const CampaignRoomStats& room) {
        return room.levelId == levelId;
    });
    if (found == rooms_.end() || !found->visited || outcome_ != CampaignOutcome::InProgress) {
        return false;
    }
    ++found->kills;
    return true;
}

void CampaignRunState::Complete() noexcept {
    if (outcome_ == CampaignOutcome::InProgress) {
        outcome_ = CampaignOutcome::Completed;
    }
}

void CampaignRunState::Fail() noexcept {
    if (outcome_ == CampaignOutcome::InProgress) {
        outcome_ = CampaignOutcome::PlayerDied;
    }
}

const CampaignRoomStats* CampaignRunState::FindRoom(const std::string_view levelId) const noexcept {
    const auto found = std::ranges::find_if(rooms_, [levelId](const CampaignRoomStats& room) {
        return room.levelId == levelId;
    });
    return found == rooms_.end() ? nullptr : &*found;
}

} // namespace fps
