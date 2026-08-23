#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fps {

enum class CampaignOutcome {
    InProgress,
    Completed,
    PlayerDied,
};

struct CampaignRoomDefinition final {
    std::string levelId;
    std::string levelName;
};

struct CampaignRoomStats final {
    std::string levelId;
    std::string levelName;
    std::size_t kills = 0;
    bool visited = false;
};

class CampaignRunState final {
public:
    [[nodiscard]] bool Initialize(
        std::span<const CampaignRoomDefinition> rooms,
        std::string& error);
    void ResetRun() noexcept;

    [[nodiscard]] bool EnterRoom(std::string_view levelId) noexcept;
    [[nodiscard]] bool RecordKill(std::string_view levelId) noexcept;
    void Complete() noexcept;
    void Fail() noexcept;

    [[nodiscard]] CampaignOutcome GetOutcome() const noexcept { return outcome_; }
    [[nodiscard]] std::span<const CampaignRoomStats> GetRooms() const noexcept { return rooms_; }
    [[nodiscard]] const CampaignRoomStats* FindRoom(std::string_view levelId) const noexcept;

private:
    std::vector<CampaignRoomStats> rooms_;
    CampaignOutcome outcome_ = CampaignOutcome::InProgress;
};

} // namespace fps
