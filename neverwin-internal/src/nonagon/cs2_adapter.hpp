#pragma once
#include "ragebot.hpp"
#include "resolver.hpp"
#include "../memory.hpp"
#include "../offsets.hpp"
#include "../entities.hpp"
#include <vector>

namespace nonagon_cs2 {

    inline Vec3 ToNonagonVec(const ent::Vector3& v) { return Vec3{v.x, v.y, v.z}; }
    inline ent::Vector3 ToEntVec(const Vec3& v) { return ent::Vector3{v.x, v.y, v.z}; }

    class CS2Player : public IPlayer {
    public:
        CS2Player(uintptr_t pawn_, int idx_, uint8_t localTeam_, uintptr_t entityList_)
            : pawn(pawn_), idx(idx_), localTeam(localTeam_), entityList(entityList_) {}

        Vec3 GetEyePos() const override {
            ent::Vector3 eye = ent::GetEyePosition(pawn);
            return ToNonagonVec(eye);
        }

        Vec3 GetHitboxPos(HitboxID h) const override {
            const auto& off = offsets::g;
            uintptr_t node = mem::Read<uintptr_t>(pawn + off.m_pGameSceneNode);
            if (!node) return Vec3{};
            ent::Vector3 origin = mem::Read<ent::Vector3>(node + off.m_vecAbsOrigin);
            float addZ = 0.f;
            switch (h) {
                case HITBOX_HEAD: addZ = 72.f; break;
                case HITBOX_NECK: addZ = 65.f; break;
                case HITBOX_CHEST: addZ = 55.f; break;
                case HITBOX_STOMACH: addZ = 40.f; break;
                case HITBOX_PELVIS: addZ = 20.f; break;
                default: addZ = 50.f; break;
            }
            return Vec3{origin.x, origin.y, origin.z + addZ};
        }

        float GetHealth() const override {
            return (float)mem::Read<int>(pawn + offsets::g.m_iHealth);
        }

        float GetSpeed() const override {
            ent::Vector3 vel = mem::Read<ent::Vector3>(pawn + offsets::g.m_vecAbsVelocity);
            return std::sqrtf(vel.x*vel.x + vel.y*vel.y + vel.z*vel.z);
        }

        bool IsAlive() const override {
            int hp = mem::Read<int>(pawn + offsets::g.m_iHealth);
            uint8_t ls = mem::Read<uint8_t>(pawn + offsets::g.m_lifeState);
            if (hp <= 0 || hp > 1000) return false;
            if (ls != 0) return false;
            uintptr_t node = mem::Read<uintptr_t>(pawn + offsets::g.m_pGameSceneNode);
            if (!node) return false;
            if (mem::Read<uint8_t>(node + offsets::g.m_bDormant) != 0) return false;
            return true;
        }

        bool IsTeammate() const override {
            uint8_t team = ent::GetTeam(pawn);
            return team == localTeam;
        }

        bool IsVisible(HitboxID h) const override {
            (void)h;
            return true;
        }

        float GetArmor() const override {
            return (float)mem::Read<int>(pawn + offsets::g.m_ArmorValue);
        }

        float GetSimTime() const override {
            return mem::Read<float>(pawn + offsets::g.m_flSimulationTime);
        }

        int GetIndex() const override { return idx; }

        uintptr_t pawn;
        int idx;
        uint8_t localTeam;
        uintptr_t entityList;
    };

    class CS2LocalPlayer : public ILocalPlayer {
    public:
        CS2LocalPlayer(uintptr_t pawn_, uintptr_t clientBase_, uint8_t team_, uintptr_t entityList_)
            : pawn(pawn_), clientBase(clientBase_), team(team_), entityList(entityList_) {}

        Vec3 GetEyePos() const override {
            ent::Vector3 eye = ent::GetEyePosition(pawn);
            return ToNonagonVec(eye);
        }

        Vec3 GetHitboxPos(HitboxID h) const override {
            uintptr_t node = mem::Read<uintptr_t>(pawn + offsets::g.m_pGameSceneNode);
            if (!node) return Vec3{};
            ent::Vector3 origin = mem::Read<ent::Vector3>(node + offsets::g.m_vecAbsOrigin);
            float addZ = 0.f;
            switch (h) {
                case HITBOX_HEAD: addZ = 72.f; break;
                case HITBOX_NECK: addZ = 65.f; break;
                case HITBOX_CHEST: addZ = 55.f; break;
                case HITBOX_STOMACH: addZ = 40.f; break;
                case HITBOX_PELVIS: addZ = 20.f; break;
                default: addZ = 50.f; break;
            }
            return Vec3{origin.x, origin.y, origin.z + addZ};
        }

        float GetHealth() const override { return (float)mem::Read<int>(pawn + offsets::g.m_iHealth); }
        float GetSpeed() const override {
            ent::Vector3 vel = mem::Read<ent::Vector3>(pawn + offsets::g.m_vecAbsVelocity);
            return std::sqrtf(vel.x*vel.x + vel.y*vel.y + vel.z*vel.z);
        }
        bool IsAlive() const override {
            int hp = mem::Read<int>(pawn + offsets::g.m_iHealth);
            uint8_t ls = mem::Read<uint8_t>(pawn + offsets::g.m_lifeState);
            return hp > 0 && hp <= 1000 && ls == 0;
        }
        bool IsTeammate() const override { return false; }
        bool IsVisible(HitboxID h) const override { (void)h; return true; }
        float GetArmor() const override { return (float)mem::Read<int>(pawn + offsets::g.m_ArmorValue); }
        float GetSimTime() const override { return mem::Read<float>(pawn + offsets::g.m_flSimulationTime); }
        int GetIndex() const override { return 0; }

        Vec3 GetViewAngles() const override {
            float pitch = mem::Read<float>(clientBase + offsets::g.dwViewAngles);
            float yaw = mem::Read<float>(clientBase + offsets::g.dwViewAngles + 4);
            return Vec3{pitch, yaw, 0.f};
        }

        void SetViewAngles(const Vec3& ang) override {
            mem::Write<float>(clientBase + offsets::g.dwViewAngles, ang.x);
            mem::Write<float>(clientBase + offsets::g.dwViewAngles + 4, ang.y);
        }

        float GetWeaponDamage() const override {
            uintptr_t weaponServices = mem::Read<uintptr_t>(pawn + offsets::g.m_pWeaponServices);
            if (!weaponServices) return 35.f;
            uint32_t handle = mem::Read<uint32_t>(weaponServices + offsets::g.m_hActiveWeapon);
            if (!handle) return 35.f;
            uintptr_t weapon = ent::GetEntityByHandle(entityList, handle);
            if (!weapon) return 35.f;
            uintptr_t vdata = mem::Read<uintptr_t>(weapon + offsets::g.m_pWeaponVData);
            if (!vdata) return 35.f;
            int dmg = mem::Read<int>(vdata + offsets::g.m_nDamage);
            return dmg > 0 ? (float)dmg : 35.f;
        }

        float GetWeaponRange() const override {
            uintptr_t weaponServices = mem::Read<uintptr_t>(pawn + offsets::g.m_pWeaponServices);
            if (!weaponServices) return 8192.f;
            uint32_t handle = mem::Read<uint32_t>(weaponServices + offsets::g.m_hActiveWeapon);
            if (!handle) return 8192.f;
            uintptr_t weapon = ent::GetEntityByHandle(entityList, handle);
            if (!weapon) return 8192.f;
            uintptr_t vdata = mem::Read<uintptr_t>(weapon + offsets::g.m_pWeaponVData);
            if (!vdata) return 8192.f;
            float range = mem::Read<float>(vdata + offsets::g.m_flRange);
            return range > 0.f ? range : 8192.f;
        }

        bool CanFire() const override {
            uintptr_t weaponServices = mem::Read<uintptr_t>(pawn + offsets::g.m_pWeaponServices);
            if (!weaponServices) return false;
            uint32_t handle = mem::Read<uint32_t>(weaponServices + offsets::g.m_hActiveWeapon);
            if (!handle) return false;
            uintptr_t weapon = ent::GetEntityByHandle(entityList, handle);
            if (!weapon) return false;
            int clip = mem::Read<int>(weapon + offsets::g.m_iClip1);
            bool reloading = mem::Read<uint8_t>(weapon + offsets::g.m_bInReload) != 0;
            return clip > 0 && !reloading;
        }

        bool IsScoped() const override {
            return mem::Read<uint8_t>(pawn + offsets::g.m_bIsScoped) != 0;
        }

        int GetWeaponIndex() const override { return 0; }
        float GetSpread() const override { return 0.01f; }
        float GetInaccuracy() const override { return 0.01f; }

        void ForceAttack() override {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            SendInput(1, &input, sizeof(INPUT));
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &input, sizeof(INPUT));
        }

        void StopMovement() override {}

        bool IsOnGround() const override {
            uint32_t flags = mem::Read<uint32_t>(pawn + offsets::g.m_fFlags);
            return (flags & 1u) != 0;
        }

        uintptr_t pawn;
        uintptr_t clientBase;
        uint8_t team;
        uintptr_t entityList;
    };

    struct NonagonRage {
        RagebotConfig cfg;
        Resolver resolver;
        std::array<std::vector<BacktrackRecord>, 64> backtrack;

        NonagonRage() {
            cfg.enabled = true;
            cfg.auto_fire = true;
            cfg.max_backtrack_ticks = 12;
            cfg.head_misses_trigger = 3;
            for (int i = 0; i < RagebotConfig::WEAPON_SLOTS; ++i) {
                cfg.weapons[i].enabled = true;
                cfg.weapons[i].max_fov = 180.f;
                cfg.weapons[i].hitchance = 50;
                cfg.weapons[i].min_damage = 1;
                cfg.weapons[i].hitboxes = (1 << HITBOX_HEAD) | (1 << HITBOX_CHEST);
            }
        }

        void OnMiss(int idx, HitboxID hb, float yaw) { resolver.OnMiss(idx, hb, yaw); }
        void OnHit(int idx) { resolver.OnHit(idx); }
    };

    inline NonagonRage& GetNonagon() {
        static NonagonRage inst;
        return inst;
    }

    inline AimTarget SelectTargetWithResolver(ILocalPlayer* local, const std::vector<IPlayer*>& players, const WeaponConfig& wcfg, Resolver& resolver, int headMissesTrigger)
    {
        AimTarget best;
        Vec3 eye = local->GetEyePos();
        Vec3 view = local->GetViewAngles();

        for (auto* p : players)
        {
            if (!p || !p->IsAlive() || p->IsTeammate()) continue;
            int idx = p->GetIndex();
            float speed = p->GetSpeed();
            float resolvedYaw = resolver.Resolve(idx, 0.f, speed, headMissesTrigger);
            (void)resolvedYaw;

            for (HitboxID hb : {HITBOX_HEAD, HITBOX_CHEST, HITBOX_STOMACH, HITBOX_PELVIS})
            {
                if (!(wcfg.hitboxes & (1 << hb))) continue;
                Vec3 pos = p->GetHitboxPos(hb);
                Vec3 delta = pos - eye;
                float dist = delta.len();
                if (dist < 1.f) continue;
                Vec3 ang;
                ang.x = std::asin(delta.z / dist) * 180.f / 3.14159265f;
                ang.y = std::atan2(delta.y, delta.x) * 180.f / 3.14159265f;
                float fov = std::sqrt((ang.x-view.x)*(ang.x-view.x) + (ang.y-view.y)*(ang.y-view.y));
                if (fov > wcfg.max_fov) continue;
                float dmg = 50.f;
                if (fov < best.fov) {
                    best.playerIndex = idx;
                    best.hitbox = hb;
                    best.aimPos = pos;
                    best.damage = dmg;
                    best.fov = fov;
                    best.valid = true;
                }
            }
        }
        return best;
    }
}
