#pragma once

namespace settings {
    struct config {
        bool log = true;

        float HoldDuration = 0.18f;
        // bool queuePowerAttacks = false;

        bool eldenCounterMode = false; //clears the blocking state if the block isn't held and player is blocking to do an attack as opposed to default bash logic.
        int modifierKey = -1;
        bool modifierRightMode = false; //modifier+righthand input power attack mode. 

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
        setting_definition<bool>{ "modifierRightMode", &settings::config::modifierRightMode },
    };

    config Get();
    void Set(const config& value);
    void SetModifierKeyFromInput(int keyCode);
    void Load();
    bool Save();

    void __stdcall RenderMenuPage();

    void RegisterMenu();
}
