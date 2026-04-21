#pragma once
#include <windows.h>
#include <cstdint>
#include "fuzzy_pid.hpp"

namespace auto_aim {

namespace Send {
    enum class Error { Success = 0, InvalidArgument = 1, DeviceNotFound = 2, DriverError = 3 };
    enum class SendType { SendInput = 0, Logitech = 1, Razer = 2, DD = 3, MouClassInputInjection = 4, LogitechGHubNew = 5, AnyDriver = 100 };
    enum class MoveMode { Absolute = 0, Relative = 1 };
    enum class InitFlags {};
}

class MouseController {
public:
    MouseController();
    ~MouseController();
    
    bool isInitialized() const { return initialized_; }
    Send::Error lastError() const { return last_error_; }
    
    void moveRelative(int dx, int dy);
    void moveSmooth(double target_x, double target_y, double dt, double aim_speed, double aim_smoothing);
    
    void resetPid() { pid_.reset(); }
    FuzzyPID& pid() { return pid_; }

private:
    bool loadDll();
    bool initDriver();
    
    FuzzyPID pid_;
    HMODULE h_dll_ = nullptr;
    bool initialized_ = false;
    Send::Error last_error_ = Send::Error::Success;
    
    using pIbSendInit = Send::Error(__stdcall*)(Send::SendType, Send::InitFlags, void*);
    using pIbSendDestroy = void(__stdcall*)();
    using pIbSendMouseMove = bool(__stdcall*)(uint32_t, uint32_t, Send::MoveMode);
    
    pIbSendInit init_fn_ = nullptr;
    pIbSendDestroy destroy_fn_ = nullptr;
    pIbSendMouseMove move_fn_ = nullptr;
};

}
