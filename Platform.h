#pragma once
#include <Windows.h>
#include <string>
#include <tuple>

// Объявление внешней переменной
extern HWND g_hwnd;

// Клавиатура
void PressKey(int vk);
void ReleaseKey(int vk);

// Мышь
void MoveMouseRelative(int dx, int dy);
void MoveMouseTo(int x, int y);
void PressMouseButton(int button);
void ReleaseMouseButton(int button);
bool IsModifierKeyPressed(const std::string& mod);
bool IsMouseButtonDown(int button);

// COM-порт управление
bool ConnectToComPort(int portNumber);
void DisconnectComPort();
bool IsComPortConnected();
int GetCurrentComPort();