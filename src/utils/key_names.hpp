#pragma once
#include <string>
#include <windows.h>

namespace auto_aim {

inline std::string GetKeyName(int vk) {
    if (vk == 0) return "NONE";
    if (vk == VK_LBUTTON) return "LMB";
    if (vk == VK_RBUTTON) return "RMB";
    if (vk == VK_MBUTTON) return "MMB";
    if (vk == VK_XBUTTON1) return "X1";
    if (vk == VK_XBUTTON2) return "X2";
    if (vk == VK_SHIFT) return "SHIFT";
    if (vk == VK_LSHIFT) return "LSHIFT";
    if (vk == VK_MENU) return "ALT";
    if (vk == VK_CONTROL) return "CTRL";
    
    char name[128];
    UINT scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    int result = GetKeyNameTextA(scanCode << 16, name, 128);
    if (result > 0) return std::string(name);
    
    return "VK_" + std::to_string(vk);
}

} // namespace auto_aim
