#pragma once

namespace settings {
    struct config {
        bool log = true;

        float HoldDuration = 0.18f;
        // bool queuePowerAttacks = false;

        bool eldenCounterMode = false; //while blocking, cancel block for an attack only when cancelBlockKey is held.
        int modifierKey = -1;
        int cancelBlockKey = -1; //if pressed and elden counter mode is enabled, the right attack input will cancel the block and do attack/power attack.
        bool modifierRightMode = false; //modifier+righthand input power attack mode. 

        bool useAltPowerKeyBind = false;
        int altPowerKey = -1;
    };
    
    template <class T>
    struct setting_definition {
        const char* key;
        T settings::config::*member;
    };

    constexpr auto setting_definitions = std::tuple{
        setting_definition<bool>{ "log", &settings::config::log },
        setting_definition<float>{ "HoldDuration", &settings::config::HoldDuration },
        setting_definition<bool>{ "eldenCounterMode", &settings::config::eldenCounterMode },
        setting_definition<int>{ "modifierKey", &settings::config::modifierKey },
        setting_definition<int>{ "cancelBlockKey", &settings::config::cancelBlockKey },
        setting_definition<bool>{ "modifierRightMode", &settings::config::modifierRightMode },
    };

    config Get();
    void Set(const config& value);
    void SetModifierKeyFromInput(int keyCode);
    void SetCancelBlockKeyFromInput(int keyCode);
    void Load();
    bool Save();

    void __stdcall RenderMenuPage();

    void RegisterMenu();
}
