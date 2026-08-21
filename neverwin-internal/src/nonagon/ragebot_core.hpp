#pragma once
#include "ragebot.hpp"
#include "resolver.hpp"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <limits>

class Ragebot
{
public:
    explicit Ragebot(RagebotConfig& cfg) : m_cfg(cfg), m_pendingAngles{0,0,0}, m_hasPending(false) {}

    void UpdateBacktrack(const std::vector<IPlayer*>& players, int currentTick)
    {
        for (auto* p : players)
        {
            if (!p || !p->IsAlive() || p->IsTeammate()) continue;
            int idx = p->GetIndex();
            if (idx < 0 || idx >= 64) continue;
            BacktrackRecord rec;
            for (int h = 0; h < HITBOX_MAX; h++)
                rec.hitboxPos[h] = p->GetHitboxPos(static_cast<HitboxID>(h));
            rec.tick = currentTick;
            rec.simtime = p->GetSimTime();
            rec.valid = true;
            auto& buf = m_backtrack[idx];
            buf.push_back(rec);
            while ((int)buf.size() > m_cfg.max_backtrack_ticks)
                buf.erase(buf.begin());
        }
    }

    AimTarget FindBestTarget(ILocalPlayer* local, const std::vector<IPlayer*>& players, const WeaponConfig& wcfg, int currentTick)
    {
        AimTarget best;
        Vec3 eye = local->GetEyePos();
        Vec3 view = local->GetViewAngles();
        int minDmg = wcfg.min_damage_override ? wcfg.min_damage_override_value : wcfg.min_damage;
        int hcReq = wcfg.hitchance_override ? wcfg.hitchance_override_value : wcfg.hitchance;

        for (auto* p : players)
        {
            if (!p || !p->IsAlive() || p->IsTeammate()) continue;
            int idx = p->GetIndex();
            float serverYaw = 0.f; // we don't have server yaw, use 0 and let resolver offset
            float speed = p->GetSpeed();
            float resolvedYaw = m_resolver.Resolve(idx, serverYaw, speed, m_cfg.head_misses_trigger);
            (void)resolvedYaw;

            auto tryHitbox = [&](HitboxID hb, bool isBody) {
                if (!(wcfg.hitboxes & (1 << hb))) return;
                if (hb == HITBOX_HEAD && wcfg.force_baim) return;
                if (!p->IsVisible(hb)) return;
                Vec3 pos = p->GetHitboxPos(hb);
                float fov = CalcFOV(view, eye, pos);
                if (fov > wcfg.max_fov) return;
                float dmg = EstimateDamage(p, local, pos);
                if (dmg < (float)minDmg) return;
                float hc = CalcHitchance(local, pos, wcfg.point_scale, isBody);
                if (hc < (float)hcReq) return;
                if (fov < best.fov) {
                    best.playerIndex = idx;
                    best.hitbox = hb;
                    best.aimPos = pos;
                    best.damage = dmg;
                    best.fov = fov;
                    best.valid = true;
                    best.isBodyAim = isBody;
                }
            };

            tryHitbox(HITBOX_HEAD, false);
            tryHitbox(HITBOX_CHEST, true);
            tryHitbox(HITBOX_STOMACH, true);
            tryHitbox(HITBOX_PELVIS, true);

            if (!best.valid && wcfg.force_baim)
            {
                tryHitbox(HITBOX_CHEST, true);
                tryHitbox(HITBOX_PELVIS, true);
            }
            if (!best.valid && wcfg.prefer_safe)
            {
                tryHitbox(HITBOX_CHEST, true);
            }
        }
        return best;
    }

    Resolver& GetResolver() { return m_resolver; }
    Vec3 GetPendingAngles() const { return m_pendingAngles; }
    bool HasPending() const { return m_hasPending; }
    void ClearPending() { m_hasPending = false; }

private:
    RagebotConfig& m_cfg;
    Resolver m_resolver;
    std::array<std::vector<BacktrackRecord>, 64> m_backtrack;
    Vec3 m_pendingAngles;
    bool m_hasPending;

    static float NormalizeYaw(float y) { while (y>180) y-=360; while (y<-180) y+=360; return y; }
    static Vec3 CalcAngle(const Vec3& src, const Vec3& dst)
    {
        Vec3 delta = dst - src;
        float dist = delta.len();
        Vec3 ang;
        const float horizontal = std::fmax(std::sqrt(delta.x*delta.x + delta.y*delta.y), 1.f);
        ang.x = std::atan2(-delta.z, horizontal) * (180.f/3.14159265f);
        ang.y = std::atan2(delta.y, delta.x) * (180.f/3.14159265f);
        ang.z = 0.f;
        return ang;
    }
    static float CalcFOV(const Vec3& view, const Vec3& src, const Vec3& dst)
    {
        Vec3 aim = CalcAngle(src,dst);
        Vec3 d = {aim.x-view.x, NormalizeYaw(aim.y-view.y), 0.f};
        return std::sqrt(d.x*d.x + d.y*d.y);
    }
    static float EstimateDamage(IPlayer* target, ILocalPlayer* local, const Vec3& pos)
    {
        float base = local->GetWeaponDamage();
        float armor = target->GetArmor();
        float dist = (pos - local->GetEyePos()).len();
        float range = local->GetWeaponRange();
        float falloff = (range>0) ? std::max(0.f, 1.f - dist/range) : 1.f;
        float armored = (armor>0) ? 0.5f : 1.f;
        return base * falloff * armored;
    }
    static float CalcHitchance(ILocalPlayer* local, const Vec3& pos, float pointScale, bool isBody)
    {
        float spread = local->GetSpread() + local->GetInaccuracy();
        if (spread < 0.0001f) return 100.f;
        float radius = isBody ? (40.f*pointScale) : 4.f;
        float dist = (pos - local->GetEyePos()).len();
        float angSize = std::atan2(radius, dist>0?dist:1.f);
        float ratio = angSize / spread;
        return std::clamp(ratio*100.f, 0.f, 100.f);
    }
};
