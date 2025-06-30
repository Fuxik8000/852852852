#pragma once
#include <sol/sol.hpp>
#include <string>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();

    // Управление потоком задач
    void StartTaskProcessing();
    void StopTaskProcessing();

    // Добавление задачи в очередь
    template <typename Func>
    void PostTask(Func&& task) {
        {
            std::unique_lock<std::mutex> lock(taskMutex);
            taskQueue.push(std::forward<Func>(task));
        }
        taskCondition.notify_one();
    }

    void ReloadScript();
    void CallOnEvent(const std::string& event, int arg);
    bool IsModifierPressed(const std::string& mod);
    bool IsMouseButtonPressed(int button);
    void SetRandomizationEnabled(bool enabled);

    // API для взаимодействия с главным потоком
    template <typename Func>
    void PostMainThreadTask(Func&& task) {
        std::lock_guard<std::mutex> lock(mainThreadTaskMutex);
        mainThreadTaskQueue.push(std::forward<Func>(task));
    }

    void ExecuteMainThreadTasks();

private:
    void TaskProcessingLoop();
    void BindAPI();
    int KeyStringToVk(const std::string& key);

    // GHUB-совместимые функции
    void PressKey(const std::string& key);
    void ReleaseKey(const std::string& key);
    void PressAndReleaseKey(const std::string& key);
    bool hzCf681ZWWcx();
    void EnablePrimaryMouseButtonEvents(bool enable);
    bool IsPrimaryMouseButtonEventsEnabled();

    sol::state lua;
    bool primary_mouse_enabled = true;
    bool randomizationEnabled = false;

    // Система задач для потока Lua
    std::queue<std::function<void()>> taskQueue;
    std::mutex taskMutex;
    std::condition_variable taskCondition;
    std::atomic<bool> taskThreadRunning{ false };
    std::thread taskThread;

    // Система задач для главного потока
    std::queue<std::function<void()>> mainThreadTaskQueue;
    std::mutex mainThreadTaskMutex;
};