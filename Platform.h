#pragma once
#include <Windows.h>
#include <string>
#include <tuple>

// ���������� ������� ����������
extern HWND g_hwnd;

// ����������
void PressKey(int vk);
void ReleaseKey(int vk);

// ����
void MoveMouseRelative(int dx, int dy);
void MoveMouseTo(int x, int y);
void PressMouseButton(int button);
void ReleaseMouseButton(int button);
bool IsModifierKeyPressed(const std::string& mod);
bool IsMouseButtonDown(int button);

// COM-���� ����������
bool ConnectToComPort(int portNumber);
void DisconnectComPort();
bool IsComPortConnected();
int GetCurrentComPort();