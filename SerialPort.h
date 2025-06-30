#pragma once
#include <Windows.h>
#include <string>
#include <vector>

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    bool Connect(int portNumber);
    void Disconnect();
    bool IsConnected() const;
    int GetCurrentPort() const;

    // Отправка команд для ESP32
    bool SendMouseMove(int8_t dx, int8_t dy);
    bool SendMousePress(uint8_t button);
    bool SendMouseRelease(uint8_t button);

    // Общая отправка данных
    bool SendData(const uint8_t* data, size_t size);

private:
    HANDLE hSerial;
    int currentPort;
    bool connected;

    bool ConfigurePort();
    void LogError(const std::string& operation);
};