#pragma once
#include "app/BitStream.h"
#include <cstdint>

namespace engine::app {

struct GameContext;

class IGameMode {
public:
    virtual ~IGameMode() = default;

    virtual void onRoundStart(GameContext& ctx)                                    = 0;
    virtual void onRoundTick(GameContext& ctx, float dt)                           = 0;
    virtual void onRoundEnd(GameContext& ctx)                                      = 0;
    virtual void onMatchEnd(GameContext& ctx)                                      = 0;
    virtual void onPlayerJoin(GameContext& ctx, uint32_t playerId)                 = 0;
    virtual void onPlayerLeave(GameContext& ctx, uint32_t playerId)                = 0;
    virtual void onPlayerSpawn(GameContext& ctx, uint32_t playerId)                = 0;
    virtual void onPlayerDeath(GameContext& ctx, uint32_t playerId,
                               uint32_t killerPlayerId)                            = 0;
    virtual void serializeState(BitStreamWriter& bsw)   const                     = 0;
    virtual void deserializeState(BitStreamReader& bsr)                            = 0;
    virtual bool evaluateWinCondition(GameContext& ctx)                            = 0;
};

} // namespace engine::app
