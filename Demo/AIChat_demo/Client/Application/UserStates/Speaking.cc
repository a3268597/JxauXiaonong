#include "Speaking.h"
#include "../../Utils/user_log.h"
#include "../../Events/AppEvents.h"
#include "../../third_party/snowboy/include/snowboy-detect-c-wrapper.h"
#include <chrono>

// 静态成员变量定义
std::atomic<bool> SpeakingState::state_running_{false};
std::thread SpeakingState::state_running_thread_;
std::atomic<bool> SpeakingState::interrupt_running_{false};
std::thread SpeakingState::interrupt_thread_;
std::atomic<long long> SpeakingState::start_tick_ms_{0};

// 刚起播时扬声器声音会串进麦克风, 这段窗口内的命中一律丢弃, 避免自我打断
static const long long ECHO_GUARD_MS = 600;

static long long _now_ms(void) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SpeakingState::Enter(Application* app) {
    std::string json_message = R"({"type": "state", "state": "speaking"})";
    app->ws_client_.SendText(json_message);
    // start播放
    app->audio_processor_.startPlaying();
    // 打断需要: 播报期间麦克风保持采集, 并另起一线程做唤醒词检测
    app->audio_processor_.startRecording();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    app->audio_processor_.clearRecordedAudioQueue();
    start_tick_ms_.store(_now_ms());
    interrupt_running_.store(true);
    interrupt_thread_ = std::thread([app]() { RunInterruptDetection(app); });
    // running
    state_running_.store(true);
    state_running_thread_ = std::thread([app]() { Run(app); });
    USER_LOG_INFO("Into speaking state.");
}

void SpeakingState::Run(Application* app) {
    USER_LOG_INFO("Speaking state run.");
    while(state_running_.load() == true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if(app->get_tts_completed() && app->audio_processor_.playbackQueueIsEmpty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            USER_LOG_INFO("Speaking end.");
            if(app->get_dialogue_completed() == false) {
                app->eventQueue_.Enqueue(static_cast<int>(AppEvent::speaking_end));
            } else {
                app->eventQueue_.Enqueue(static_cast<int>(AppEvent::dialogue_end));
            }
            app->set_tts_completed(false);
            app->set_dialogue_completed(false);
            break;
        }
    }
}

void SpeakingState::RunInterruptDetection(Application* app) {
    USER_LOG_INFO("Interrupt detection run.");
    SnowboyDetect* detector = SnowboyDetectConstructor("third_party/snowboy/resources/common.res",
                                                       "third_party/snowboy/resources/models/xiaonong.pmdl");
    SnowboyDetectSetSensitivity(detector, "0.7");
    SnowboyDetectSetAudioGain(detector, 1);
    SnowboyDetectApplyFrontend(detector, false);
    std::vector<int16_t> data;
    while (interrupt_running_.load() == true) {
        if (app->audio_processor_.recordedQueueIsEmpty() == false) {
            if (app->audio_processor_.getRecordedAudio(data)) {
                if (_now_ms() - start_tick_ms_.load() < ECHO_GUARD_MS) {
                    // 回声窗口内: 丢弃这一段音频并复位检测器, 不做判定
                    SnowboyDetectReset(detector);
                    continue;
                }
                if (SnowboyDetectRunDetection(detector, data.data(), data.size(), false) > 0) {
                    USER_LOG_INFO("Wake detected while speaking, interrupting.");
                    // 立刻静音, 不等状态机下一个 100ms 轮询
                    app->audio_processor_.clearPlaybackAudioQueue();
                    // 通知服务端停止生成与推送剩余音频
                    app->ws_client_.SendText(R"({"type": "interrupt"})");
                    app->eventQueue_.Enqueue(static_cast<int>(AppEvent::wake_detected));
                    break;
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    SnowboyDetectDestructor(detector);
    USER_LOG_INFO("Interrupt detection stopped.");
}

void SpeakingState::Exit(Application* app) {
    // stop打断检测
    interrupt_running_.store(false);
    if (interrupt_thread_.joinable()) {
        interrupt_thread_.join();
    }
    // clear playback audio queue
    app->audio_processor_.clearPlaybackAudioQueue();
    // 复位播报完成标志, 否则下一轮 speaking 可能被上一轮的残留状态提前结束
    app->set_tts_completed(false);
    // stop播放
    app->audio_processor_.stopPlaying();
    // stop录音
    app->audio_processor_.stopRecording();
    // stop state running
    state_running_.store(false);
    state_running_thread_.join();
    USER_LOG_INFO("Speaking exit.");
}
