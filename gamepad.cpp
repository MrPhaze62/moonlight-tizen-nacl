#include "moonlight.hpp"
#include "ppapi/c/ppb_gamepad.h"
#include <Limelight.h>
#include <sstream>

// Dead zone threshold for axes
#define AXIS_DEAD_ZONE 0.1f

static const unsigned short k_StandardGamepadButtonMapping[] = {
    A_FLAG, B_FLAG, X_FLAG, Y_FLAG,
    LB_FLAG, RB_FLAG,
    0, 0, // Indices 6,7: triggers handled via axis on older Tizen
    BACK_FLAG, PLAY_FLAG,
    LS_CLK_FLAG, RS_CLK_FLAG,
    UP_FLAG, DOWN_FLAG, LEFT_FLAG, RIGHT_FLAG,
    SPECIAL_FLAG
};

static float ApplyDeadZone(float value) {
    if (value > -AXIS_DEAD_ZONE && value < AXIS_DEAD_ZONE) {
        return 0.0f;
    }
    return value;
}

static short GetActiveGamepadMask(PP_GamepadsSampleData& gamepadData) {
    short controllerIndex = 0;
    short activeGamepadMask = 0;

    for (unsigned int p = 0; p < gamepadData.length; p++) {
        PP_GamepadSampleData& padData = gamepadData.items[p];

        printf("[NaCl, GetActiveGamepadMask] Gamepad %u: connected = %d, timestamp = %f\n",
               p, padData.connected, padData.timestamp);

        if (!padData.connected) {
            continue;
        }

        activeGamepadMask |= (1 << controllerIndex);
        controllerIndex++;
    }

    return activeGamepadMask;
}

void MoonlightInstance::PollGamepads() {
    PP_GamepadsSampleData gamepadData;
    short controllerIndex = 0;
    short activeGamepadMask;

    m_GamepadApi->Sample(pp_instance(), &gamepadData);
    activeGamepadMask = GetActiveGamepadMask(gamepadData);

    printf("[NaCl] Active gamepad mask: %d\n", activeGamepadMask);

    for (unsigned int p = 0; p < gamepadData.length; p++) {
        PP_GamepadSampleData& padData = gamepadData.items[p];

        if (!padData.connected) {
            continue;
        }

        // Tizen 4.0 may report timestamp=0 permanently — always send if so
        if (padData.timestamp != 0 && padData.timestamp == m_LastPadTimestamps[p]) {
            controllerIndex++;
            continue;
        }

        m_LastPadTimestamps[p] = padData.timestamp;

        printf("[NaCl] Gamepad %u: axes_length=%u buttons_length=%u\n",
               p, padData.axes_length, padData.buttons_length);

        int buttonFlags = 0;
        unsigned char leftTrigger = 0, rightTrigger = 0;
        short leftStickX = 0, leftStickY = 0;
        short rightStickX = 0, rightStickY = 0;

        // Detect axis layout:
        // 6-axis = old Tizen layout: axes[0-1]=LS, axes[2]=combined triggers, axes[3-4]=RS
        // 4-axis = standard layout:  axes[0-1]=LS, axes[2-3]=RS (triggers are buttons 6,7)
        bool oldTizenAxisLayout = (padData.axes_length >= 5);

        // Handle buttons (skip trigger button indices on old Tizen layout)
        for (unsigned int i = 0; i < padData.buttons_length; i++) {
            if (i >= sizeof(k_StandardGamepadButtonMapping) / sizeof(k_StandardGamepadButtonMapping[0])) {
                break;
            }

            if (oldTizenAxisLayout) {
                // On old Tizen, LT/RT come from axis[2], not buttons 6/7 — skip those
                if (i == 6 || i == 7) {
                    continue;
                }
            } else {
                // Standard layout: triggers ARE buttons 6 and 7
                if (i == 6) {
                    leftTrigger = padData.buttons[i] * 0xFF;
                    continue;
                }
                if (i == 7) {
                    rightTrigger = padData.buttons[i] * 0xFF;
                    continue;
                }
            }

            if (padData.buttons[i] > 0.5f) {
                buttonFlags |= k_StandardGamepadButtonMapping[i];
            }
        }

        if (oldTizenAxisLayout) {
            // Old Tizen Xbox layout:
            // axes[0] = left stick X
            // axes[1] = left stick Y
            // axes[2] = combined trigger axis (LT=-1..0, RT=0..1)
            // axes[3] = right stick X
            // axes[4] = right stick Y
            leftStickX = ApplyDeadZone(padData.axes[0]) * 0x7FFF;
            leftStickY = -ApplyDeadZone(padData.axes[1]) * 0x7FFF;

            float triggerAxis = padData.axes[2];
            if (triggerAxis < -AXIS_DEAD_ZONE) {
                // LT is being pressed (negative range)
                leftTrigger = (unsigned char)((-triggerAxis) * 0xFF);
            } else if (triggerAxis > AXIS_DEAD_ZONE) {
                // RT is being pressed (positive range)
                rightTrigger = (unsigned char)(triggerAxis * 0xFF);
            }

            if (padData.axes_length >= 5) {
                rightStickX = ApplyDeadZone(padData.axes[3]) * 0x7FFF;
                rightStickY = -ApplyDeadZone(padData.axes[4]) * 0x7FFF;
            }
        } else {
            // Standard layout: axes[0-1]=LS, axes[2-3]=RS
            leftStickX = ApplyDeadZone(padData.axes[0]) * 0x7FFF;
            leftStickY = -ApplyDeadZone(padData.axes[1]) * 0x7FFF;

            if (padData.axes_length >= 4) {
                rightStickX = ApplyDeadZone(padData.axes[2]) * 0x7FFF;
                rightStickY = -ApplyDeadZone(padData.axes[3]) * 0x7FFF;
            }
        }

        LiSendMultiControllerEvent(controllerIndex, activeGamepadMask,
            buttonFlags, leftTrigger, rightTrigger,
            leftStickX, leftStickY, rightStickX, rightStickY);

        controllerIndex++;
    }
}

void MoonlightInstance::ClControllerRumble(unsigned short controllerNumber,
    unsigned short lowFreqMotor, unsigned short highFreqMotor)
{
    const float weakMagnitude = static_cast<float>(highFreqMotor) / static_cast<float>(UINT16_MAX);
    const float strongMagnitude = static_cast<float>(lowFreqMotor) / static_cast<float>(UINT16_MAX);

    std::ostringstream ss;
    ss << controllerNumber << "," << weakMagnitude << "," << strongMagnitude;

    pp::Var response(std::string("controllerRumble: ") + ss.str());
    g_Instance->PostMessage(response);
}
