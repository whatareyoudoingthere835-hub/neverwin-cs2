#pragma once
#include "ragebot.hpp"
#include <array>
#include <vector>
#include <cmath>

enum class ResolveMethod : uint8_t
{
    None       = 0,
    BruteForce = 1,
    LBY        = 2,
    StaticSide = 3,
};

enum class YawSide : int8_t
{
    Left  = -1,
    None  =  0,
    Right =  1,
};

static constexpr int MAX_PLAYERS = 64;

struct PlayerResolveState
{
    int        missedShots    = 0;
    int        headMisses     = 0;
    int        bruteIndex     = 0;
    YawSide    lastSide       = YawSide::None;
    bool       flipSign       = false;
    bool       hasDesync      = false;
    float      resolvedYaw    = 0.f;
    float      lastLBY        = 0.f;
    ResolveMethod method      = ResolveMethod::None;
    float      spreadOffset   = 0.f;
    bool       overlaps       = false;
};

static constexpr float BRUTE_ANGLES[] = { 0.f, 58.f, -58.f, 120.f, -120.f, 180.f };
static constexpr int   BRUTE_COUNT    = static_cast<int>(sizeof(BRUTE_ANGLES) / sizeof(float));

class Resolver
{
public:
    void OnPlayerVisible(int idx, float serverYaw, float lbyYaw, float speed)
    {
        if (idx < 0 || idx >= MAX_PLAYERS) return;
        auto& s = m_states[idx];
        s.lastLBY = lbyYaw;

        bool moving = speed > 0.1f;

        if (!moving)
        {
            float delta = NormalizeYaw(serverYaw - lbyYaw);
            s.hasDesync = (std::fabs(delta) > 35.f);
        }
        else
        {
            s.hasDesync = false;
        }
    }

    void OnMiss(int idx, HitboxID hitbox, float resolvedYaw)
    {
        if (idx < 0 || idx >= MAX_PLAYERS) return;
        auto& s = m_states[idx];
        s.missedShots++;

        if (hitbox == HITBOX_HEAD)
            s.headMisses++;

        if (s.method == ResolveMethod::BruteForce)
        {
            s.bruteIndex = (s.bruteIndex + 1) % BRUTE_COUNT;
        }
        else
        {
            if (s.flipSign)
                s.lastSide = (s.lastSide == YawSide::Left) ? YawSide::Right : YawSide::Left;
        }
    }

    void OnHit(int idx)
    {
        if (idx < 0 || idx >= MAX_PLAYERS) return;
        auto& s = m_states[idx];
        s.missedShots  = 0;
        s.headMisses   = 0;
        s.bruteIndex   = 0;
    }

    float Resolve(int idx, float serverYaw, float speed, int headMissesTrigger)
    {
        if (idx < 0 || idx >= MAX_PLAYERS) return serverYaw;
        auto& s = m_states[idx];

        if (s.missedShots >= headMissesTrigger)
        {
            s.method = ResolveMethod::BruteForce;
        }
        else if (s.hasDesync)
        {
            s.method = ResolveMethod::LBY;
        }
        else
        {
            s.method = ResolveMethod::StaticSide;
        }

        float offset = 0.f;

        switch (s.method)
        {
        case ResolveMethod::BruteForce:
            offset = BRUTE_ANGLES[s.bruteIndex];
            break;

        case ResolveMethod::LBY:
            offset = NormalizeYaw(s.lastLBY - serverYaw);
            if (s.flipSign)
                offset = -offset;
            break;

        case ResolveMethod::StaticSide:
            offset = (s.lastSide == YawSide::Left) ? -58.f : 58.f;
            if (s.flipSign)
                offset = -offset;
            break;

        default:
            break;
        }

        s.spreadOffset  = offset;
        s.resolvedYaw   = NormalizeYaw(serverYaw + offset);
        return s.resolvedYaw;
    }

    void Reset(int idx)
    {
        if (idx < 0 || idx >= MAX_PLAYERS) return;
        m_states[idx] = PlayerResolveState{};
    }

    const PlayerResolveState& GetState(int idx) const
    {
        return m_states[idx];
    }

private:
    static float NormalizeYaw(float y)
    {
        while (y >  180.f) y -= 360.f;
        while (y < -180.f) y += 360.f;
        return y;
    }

    std::array<PlayerResolveState, MAX_PLAYERS> m_states{};
};
