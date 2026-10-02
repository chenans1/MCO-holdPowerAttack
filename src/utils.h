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

}
