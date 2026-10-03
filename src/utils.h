#pragma once
#include "settings.h"

namespace utils {
	static inline bool isRightMelee(RE::Actor* actor) {
       if (!actor) {
            return false;
        }
        int rightHand = 0;
        actor->GetGraphVariableInt("iRightHandType", rightHand);
        return (rightHand <= 6);
    }

    static inline bool isH2H(RE::Actor* actor) {
       if (!actor) {
            return false;
        }
        int rightHand = 0;
        actor->GetGraphVariableInt("iRightHandType", rightHand);
        return (rightHand == 0);
    }


    static inline bool isLeftSpell(RE::Actor* actor) {
        if (!actor) {
            return false;
        }
        int leftHand = 0;
        actor->GetGraphVariableInt("iLeftHandType", leftHand);
        return (leftHand == 8 || leftHand == 9);
    }

    static inline int toKeyCode(const RE::ButtonEvent& event) {
        const auto device = event.device.get();
        const auto id = event.GetIDCode();

        switch (device) {
            case RE::INPUT_DEVICE::kKeyboard:
                return static_cast<int>(id);

            case RE::INPUT_DEVICE::kMouse:
                return static_cast<int>(id + SKSE::InputMap::kMacro_MouseButtonOffset);

            case RE::INPUT_DEVICE::kGamepad:
                return static_cast<int>(SKSE::InputMap::GamepadMaskToKeycode(id));

            default:
                return 0;
        }
    }

    //entirely copy pasted from: https://github.com/ersh1/DodgeFramework/blob/master/src/Utils.h
    #define PI 3.14159265f
    #define PI8 0.39269908f
    static inline float DotProduct(RE::NiPoint2& a, RE::NiPoint2& b) { return a.x * b.x + a.y * b.y; }
    static inline float CrossProduct(RE::NiPoint2& a, RE::NiPoint2& b) { return a.x * b.y - a.y * b.x; }
    static inline float GetAngle(RE::NiPoint2& a, RE::NiPoint2& b) { return atan2(CrossProduct(a, b), DotProduct(a, b)); }
    static inline float Vec2Length(const RE::NiPoint2& vec) { return std::sqrtf(vec.x * vec.x + vec.y * vec.y); }

    static inline RE::NiPoint2 Vec2Normalize(RE::NiPoint2& vec) {
        RE::NiPoint2 ret(0.f, 0.f);
        float vecLength = Vec2Length(vec);
        if (vecLength == 0) {
            return ret;
        }
        float invlen = 1.0f / vecLength;
        ret.x = vec.x * invlen;
        ret.y = vec.y * invlen;
        return ret;
    }

    static inline bool isNeutral() {
        auto playerControls = RE::PlayerControls::GetSingleton();
        if (!playerControls) {
            return true;
        }
        auto normalizedInputDirection = Vec2Normalize(playerControls->data.prevMoveVec);
        const auto cfg = settings::Get();
        if (cfg.includeFWD) {
            RE::NiPoint2 forwardVector(0.f, 1.f);
            float dodgeAngle = GetAngle(normalizedInputDirection, forwardVector);
            if (dodgeAngle >= -PI8 && dodgeAngle < PI8) {
                return true;
            }
        }
        

        if (normalizedInputDirection.x == 0.f && normalizedInputDirection.y == 0.f) {
            return true;
        } else {
            return false;
        }
    }

    //need to resolve to these attackData
    struct AttackData {
        static const RE::BSFixedString& AttackStart() {
            static const RE::BSFixedString value{ "AttackStart" };
            return value;
        }
        
        static const RE::BSFixedString& AttackPowerStartInPlace() {
            static const RE::BSFixedString value{ "AttackPowerStartInPlace" };
            return value;
        }

        static const RE::BSFixedString& AttackStartH2HRight() {
            static const RE::BSFixedString value{ "AttackStartH2HRight" };
            return value;
        }

        static const RE::BSFixedString& attackPowerStartForwardH2HRightHand() {
            static const RE::BSFixedString value{ "attackPowerStartForwardH2HRightHand" };
            return value;
        }

        static const RE::BSFixedString& attackStartSprint() {
            static const RE::BSFixedString value{ "attackStartSprint" };
            return value;
        }

        static const RE::BSFixedString& AttackPowerStartSprint() {
            static const RE::BSFixedString value{ "attackPowerStart_Sprint" };
            return value;
        }

        static const RE::BSFixedString& Select(bool powerAttack, bool handToHand)
        {
            if (handToHand) {
                return powerAttack ? attackPowerStartForwardH2HRightHand() : AttackStartH2HRight();
            }

            return powerAttack ? AttackPowerStartInPlace() : AttackStart();
        }
        
    };

    // to deal with input fuckery. I need to figure out how to auto update attack data some day, likely 
    // via the perform idle function hook. 
    static inline bool forceUpdateAttackData(bool powerAttack) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        const auto currentProcess = player->GetActorRuntimeData().currentProcess;
        if (!currentProcess || !currentProcess->high) {
            return false;
        }

        auto* race = player->GetRace();
        auto* attackDataMap = race ? race->attackDataMap.get() : nullptr;
        if (!attackDataMap) {
            return false;
        }
        // const bool isSprinting = player->GetPlayerRuntimeData().playerFlags.isSprinting;
        const bool isHandToHand = isH2H(player);
        
        auto attackDataString =  AttackData::Select(powerAttack, isHandToHand);
        const auto it = attackDataMap->attackDataMap.find(attackDataString);
        if (it == attackDataMap->attackDataMap.end() || !it->second) {
            SKSE::log::info("forceUpdateAttackData failed to update");
            return false;
        }

        auto* selectedAttackData = it->second.get();
        auto& activeAttackData = currentProcess->high->attackData;
        const bool attackDataChanged = activeAttackData.get() != selectedAttackData;
        if (attackDataChanged) {
            activeAttackData.reset(selectedAttackData);
        }

        // SKSE::log::info(
        //     "forceUpdateAttackData {} '{}': powerAttack={}, staminaMult={}, damageMult={}",
        //     attackDataChanged ? "reapplied" : "already active",
        //     attackDataString.c_str(),
        //     selectedAttackData->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack),
        //     selectedAttackData->data.staminaMult,
        //     selectedAttackData->data.damageMult);
        return true;
    }
}
