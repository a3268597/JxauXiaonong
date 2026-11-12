#ifndef SPEAKING_STATE_H
#define SPEAKING_STATE_H

#include "../../Application/Application.h"
#include <vector>
#include <thread>
#include <atomic>

class SpeakingState {
public:
    static void Enter(Application* app);
    static void Run(Application* app);
    static void Exit(Application* app);
    // 播报期间持续跑唤醒词检测, 实现"说话打断"(barge-in)
    static void RunInterruptDetection(Application* app);
private:
    static std::atomic<bool> state_running_;       // 静态成员变量：运行状态标志
    static std::thread state_running_thread_;      // 静态成员变量：运行线程
    static std::atomic<bool> interrupt_running_;   // 打断检测线程运行标志
    static std::thread interrupt_thread_;          // 打断检测线程
    static std::atomic<long long> start_tick_ms_;  // 进入本状态的时刻, 用于跳过外放回声窗口
};

#endif // SPEAKING_STATE_H
