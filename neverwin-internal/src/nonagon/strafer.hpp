#pragma once
#include "ragebot.hpp"
#include <cmath>
#include <algorithm>

struct StraferConfig
{
    bool  enabled           = false;
    bool  test_strafer      = false;
    bool  airstrafe         = false;
    bool  fully_directional = false;
    bool  jumpbug           = false;
    bool  edgejump          = false;
    bool  edgestop          = false;
    bool  slowwalk          = false;
    float slowwalk_speed    = 100.f;
};

class TestStrafer
{
public:
    explicit TestStrafer(StraferConfig& cfg) : m_cfg(cfg) {}

    struct Input
    {
        Vec3  velocity;
        Vec3  viewAngles;
        float wishYaw;
        float maxSpeed;
        float friction;
        float accel;
        bool  onGround;
    };

    struct Output
    {
        float moveX;
        float moveY;
        float viewYaw;
    };

    Output Run(const Input& in)
    {
        Output out{};
        if (!m_cfg.enabled) return out;
        if (m_cfg.slowwalk)
        {
            float speed2d = std::sqrt(in.velocity.x * in.velocity.x + in.velocity.y * in.velocity.y);
            float scale = (speed2d > 0.f) ? std::min(1.f, m_cfg.slowwalk_speed / speed2d) : 0.f;
            out.moveX = scale; out.moveY = 0.f; out.viewYaw = in.viewAngles.y;
            return out;
        }
        if (!m_cfg.test_strafer) return out;
        return RunTestStrafer(in);
    }

    void OnJump(ILocalPlayer* local) { (void)local; }

private:
    StraferConfig& m_cfg;
    bool m_side = false;

    static constexpr float DEG2RAD = 3.14159265f / 180.f;
    static constexpr float RAD2DEG = 180.f / 3.14159265f;

    static float NormAngle(float a)
    {
        while (a >  180.f) a -= 360.f;
        while (a < -180.f) a += 360.f;
        return a;
    }
    static float VelToYaw(float vx, float vy) { return std::atan2(vy, vx) * RAD2DEG; }

    Output RunTestStrafer(const Input& in)
    {
        Output out{};
        float vx = in.velocity.x; float vy = in.velocity.y;
        float speed = std::sqrt(vx*vx + vy*vy);
        float maxSpeed = in.maxSpeed > 0.f ? in.maxSpeed : 250.f;
        float accel = in.accel > 0.f ? in.accel : 10.f;
        float velYaw = VelToYaw(vx, vy);
        float gaFrametime = 0.03125f;
        float maxAccel = maxSpeed * accel * gaFrametime;
        float targetYaw = in.wishYaw;
        float optAngle;
        if (speed < 0.0001f) optAngle = targetYaw + (m_side ? 90.f : -90.f);
        else {
            float cosA = std::clamp(maxAccel / speed, -1.f, 1.f);
            float theta = std::acos(cosA) * RAD2DEG;
            optAngle = m_side ? velYaw + theta : velYaw - theta;
        }
        optAngle = NormAngle(optAngle);
        float deltaYaw = NormAngle(optAngle - in.viewAngles.y);
        out.viewYaw = NormAngle(in.viewAngles.y + deltaYaw);
        out.moveX = 0.f; out.moveY = m_side ? 450.f : -450.f;
        if (speed >= 10.f) m_side = !m_side;
        return out;
    }
};
