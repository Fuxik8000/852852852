#include "Platform.h"
#include "Log.h"
#include "SerialPort.h"
#include <map>

// Определяем NOMINMAX перед включением Windows.h чтобы избежать конфликта с std::max/min
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>  // Для std::max и std::min

// Внешний глобальный HWND из main.cpp
extern HWND g_hwnd;

// Глобальный объект для работы с COM-портом
static SerialPort g_serialPort;

// Функции для управления COM-портом
bool ConnectToComPort(int portNumber) {
    return g_serialPort.Connect(portNumber);
}

void DisconnectComPort() {
    g_serialPort.Disconnect();
}

bool IsComPortConnected() {
    return g_serialPort.IsConnected();
}

int GetCurrentComPort() {
    return g_serialPort.GetCurrentPort();
}

// Реализация для int-кодов клавиш (без изменений)
void PressKey(int vk) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    SendInput(1, &input, sizeof(INPUT));
}

void ReleaseKey(int vk) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void MoveMouseRelative(int dx, int dy) {
    // Пытаемся отправить через COM-порт
    if (g_serialPort.IsConnected()) {
        // Ограничиваем значения диапазоном int8_t (-128 до 127)
        int8_t safe_dx = static_cast<int8_t>((std::max)(-128, (std::min)(127, dx)));
        int8_t safe_dy = static_cast<int8_t>((std::max)(-128, (std::min)(127, dy)));

        if (g_serialPort.SendMouseMove(safe_dx, safe_dy)) {
            OutputLogMessage("[SERIAL] Mouse move: dx=" + std::to_string(safe_dx) +
                ", dy=" + std::to_string(safe_dy) + "\n");
            return; // Успешно отправлено через COM-порт
        }
        else {
            OutputLogMessage("[SERIAL] Ошибка отправки движения мыши, используем системный курсор\n");
        }
    }

    // Fallback: используем системный курсор если COM-порт недоступен
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

void MoveMouseTo(int x, int y) {
    // Абсолютное позиционирование пока не поддерживается через COM-порт
    // Используем только системный курсор

    if (!g_hwnd) {
        OutputLogMessage("[ERROR] MoveMouseTo: g_hwnd is null\n");
        return;
    }

    // Преобразуем клиентские координаты в экранные
    POINT pt;
    pt.x = x;
    pt.y = y;

    if (!ClientToScreen(g_hwnd, &pt)) {
        OutputLogMessage("[ERROR] MoveMouseTo: ClientToScreen failed\n");
        return;
    }

    // Рассчитываем абсолютные координаты
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Разбиваем вычисления на отдельные шаги
    double normalizedX = static_cast<double>(pt.x) / screenWidth;
    double normalizedY = static_cast<double>(pt.y) / screenHeight;
    int absX = static_cast<int>(normalizedX * 65535.0);
    int absY = static_cast<int>(normalizedY * 65535.0);

    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = absX;
    input.mi.dy = absY;
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));

    OutputLogMessage("[SYSTEM] MoveMouseTo (абсолютное): x=" + std::to_string(x) +
        ", y=" + std::to_string(y) + "\n");
}

void PressMouseButton(int button) {
    // Пытаемся отправить через COM-порт
    if (g_serialPort.IsConnected()) {
        uint8_t espButton = static_cast<uint8_t>(button);
        if (g_serialPort.SendMousePress(espButton)) {
            OutputLogMessage("[SERIAL] Mouse press: button=" + std::to_string(button) + "\n");
            return; // Успешно отправлено через COM-порт
        }
        else {
            OutputLogMessage("[SERIAL] Ошибка отправки нажатия кнопки, используем системную мышь\n");
        }
    }

    // Fallback: используем системную мышь
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;

    switch (button) {
    case 1: input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
    case 2: input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break;
    case 3: input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; break;
    case 4: input.mi.dwFlags = MOUSEEVENTF_XDOWN; input.mi.mouseData = XBUTTON1; break;
    case 5: input.mi.dwFlags = MOUSEEVENTF_XDOWN; input.mi.mouseData = XBUTTON2; break;
    }

    SendInput(1, &input, sizeof(INPUT));
}

void ReleaseMouseButton(int button) {
    // Пытаемся отправить через COM-порт
    if (g_serialPort.IsConnected()) {
        uint8_t espButton = static_cast<uint8_t>(button);
        if (g_serialPort.SendMouseRelease(espButton)) {
            OutputLogMessage("[SERIAL] Mouse release: button=" + std::to_string(button) + "\n");
            return; // Успешно отправлено через COM-порт
        }
        else {
            OutputLogMessage("[SERIAL] Ошибка отправки отпускания кнопки, используем системную мышь\n");
        }
    }

    // Fallback: используем системную мышь
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;

    switch (button) {
    case 1: input.mi.dwFlags = MOUSEEVENTF_LEFTUP; break;
    case 2: input.mi.dwFlags = MOUSEEVENTF_RIGHTUP; break;
    case 3: input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP; break;
    case 4: input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON1; break;
    case 5: input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON2; break;
    }

    SendInput(1, &input, sizeof(INPUT));
}

bool IsModifierKeyPressed(const std::string& mod) {
    static std::map<std::string, int> modMap = {
        {"ctrl", VK_CONTROL},
        {"shift", VK_SHIFT},
        {"alt", VK_MENU},
        {"lctrl", VK_LCONTROL},
        {"rctrl", VK_RCONTROL},
        {"lshift", VK_LSHIFT},
        {"rshift", VK_RSHIFT},
        {"lalt", VK_LMENU},
        {"ralt", VK_RMENU}
    };

    auto it = modMap.find(mod);
    if (it == modMap.end()) return false;

    return (GetAsyncKeyState(it->second) & 0x8000) != 0;
}

bool IsMouseButtonDown(int button) {
    // Сопоставление GHUB-кнопок с физическими кнопками
    switch (button) {
    case 1: return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;   // Левая кнопка
    case 2: return (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;   // Правая кнопка
    case 3: return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;   // Средняя кнопка
    case 4: return (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;  // Боковая кнопка 1
    case 5: return (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;  // Боковая кнопка 2
    }
    return false;
}