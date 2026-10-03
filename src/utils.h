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
}
