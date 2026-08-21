#pragma once
#include <cstdint>
#include <cmath>
#include <array>
#include <vector>
#include <limits>

enum HitboxID : int
{
    HITBOX_HEAD    = 0,
    HITBOX_NECK    = 1,
    HITBOX_CHEST   = 2,
    HITBOX_STOMACH = 3,
    HITBOX_PELVIS  = 4,
    HITBOX_MAX     = 5,
};

enum AutostopMode : uint8_t
{
    AUTOSTOP_NONE  = 0,
    AUTOSTOP_FULL  = 1,
    AUTOSTOP_LEGIT = 2,
};

struct WeaponConfig
{
    bool  enabled              = true;
    bool  silent               = false;
    bool  no_spread            = false;
    bool  double_tap           = false;
    bool  force_shot_in_air    = false;
    bool  force_shot_on_ground = false;

    float   max_fov   = 180.0f;
    int     hitchance = 50;
    int     min_damage = 1;

    bool    hitchance_override        = false;
    int     hitchance_override_value  = 50;
    bool    min_damage_override       = false;
    int     min_damage_override_value = 1;

    bool    force_baim          = false;
    float   point_scale         = 1.0f;
    bool    dynamic_point_scale = false;

    uint8_t hitboxes = (1 << HITBOX_HEAD) | (1 << HITBOX_CHEST);

    AutostopMode autostop_mode      = AUTOSTOP_NONE;
    float        autostop_min_speed = 0.0f;

    bool prefer_safe = true;
};

struct RagebotConfig
{
    bool enabled     = false;
    bool auto_fire   = false;
    bool auto_scope  = false;

    int max_backtrack_ticks   = 12;
    int max_extrapolate_ticks = 3;

    int head_misses_trigger = 3;

    bool  zeusbot     = false;
    float zeusbot_fov = 180.0f;

    bool  knifebot     = false;
    float knifebot_fov = 180.0f;

    bool auto_revolver = false;

    static constexpr int WEAPON_SLOTS = 20;
    WeaponConfig weapons[WEAPON_SLOTS];
};

struct Vec3
{
    float x = 0, y = 0, z = 0;

    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator*(float s)       const { return {x*s,   y*s,   z*s};   }
    float len()   const { return std::sqrt(x*x + y*y + z*z); }
    float len2d() const { return std::sqrt(x*x + y*y); }
    Vec3 normalized() const
    {
        float l = len();
        return l > 0.f ? Vec3{x/l, y/l, z/l} : Vec3{};
    }
    float dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
};

struct BacktrackRecord
{
    Vec3  hitboxPos[HITBOX_MAX];
    int   tick;
    float simtime;
    bool  valid = false;
};

struct ShotRecord
{
    int   targetIndex;
    HitboxID hitbox;
    float estimatedDamage;
    float hitchance;
    int   tick;
    bool  isBodyAim;
};

struct AimTarget
{
    int      playerIndex = -1;
    HitboxID hitbox      = HITBOX_HEAD;
    Vec3     aimPos;
    float    damage       = 0.f;
    float    fov          = std::numeric_limits<float>::max();
    int      backtrackTick = -1;
    bool     valid         = false;
    bool     isBodyAim     = false;
};

struct IPlayer
{
    virtual Vec3  GetEyePos()                            const = 0;
    virtual Vec3  GetHitboxPos(HitboxID h)               const = 0;
    virtual float GetHealth()                            const = 0;
    virtual float GetSpeed()                             const = 0;
    virtual bool  IsAlive()                              const = 0;
    virtual bool  IsTeammate()                           const = 0;
    virtual bool  IsVisible(HitboxID h)                  const = 0;
    virtual float GetArmor()                             const = 0;
    virtual float GetSimTime()                           const = 0;
    virtual int   GetIndex()                             const = 0;
    virtual ~IPlayer() = default;
};

struct ILocalPlayer : public IPlayer
{
    virtual Vec3  GetViewAngles()                        const = 0;
    virtual void  SetViewAngles(const Vec3& ang)               = 0;
    virtual float GetWeaponDamage()                      const = 0;
    virtual float GetWeaponRange()                       const = 0;
    virtual bool  CanFire()                              const = 0;
    virtual bool  IsScoped()                             const = 0;
    virtual int   GetWeaponIndex()                       const = 0;
    virtual float GetSpread()                            const = 0;
    virtual float GetInaccuracy()                        const = 0;
    virtual void  ForceAttack()                                = 0;
    virtual void  StopMovement()                               = 0;
    virtual bool  IsOnGround()                           const = 0;
};
