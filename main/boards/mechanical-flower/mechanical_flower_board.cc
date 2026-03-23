/**
 * @file mechanical_flower_board.cc
 * @brief 机械AI仿生花开发板驱动 - 完整版本
 * 
 * 【硬件配置】
 * - 主控芯片：ESP32-C3
 * - 语音采集：INMP441 I2S MEMS麦克风
 * - 音频输出：MAX98357A I2S数字功放
 * - 运动控制：SG90舵机（GPIO9）
 * - 灯光效果：WS2812 RGB灯带（GPIO19）
 * - 按钮1（GPIO0）：单击重启，长按配网
 * - 按钮2（GPIO1）：单击打断对话
 * - 按钮3（GPIO12）：单击浇水，长按查询上次浇水时间
 * 
 * 【交互模式】
 * - 默认：唤醒词"小花小花"触发对话
 * - 按钮交互：三个按钮提供快捷功能
 * 
 * 【LED效果】
 * - 聆听状态：呼吸灯效果
 * - 待命状态：常亮
 */

#include "wifi_board.h"
#include "display/display.h"
#include "mechanical_flower_audio_codec.h"
#include "mechanical_flower_controller.h"
#include "flower_led_controller.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "press_to_talk_mcp_tool.h"
#include "mcp_server.h"
#include "protocol.h"
#include "settings.h"
#include "assets/lang_config.h"

#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
#include "blufi.h"
#endif

#include <esp_log.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ctime>

#define TAG "MechanicalFlower"

// LED灯带数量
#define LED_STRIP_COUNT  3

// 长按时间阈值（毫秒）
#define LONG_PRESS_TIME_MS  1000

class MechanicalFlowerBoard : public WifiBoard {
private:
    Button button1_;  // GPIO0：联网/重启
    Button button2_;  // GPIO1：打断对话
    Button button3_;  // GPIO12：浇水/查询浇水时间
    PressToTalkMcpTool* press_to_talk_tool_ = nullptr;
    bool button1_long_press_triggered_ = false;  // 防止长按后触发单击

    void InitializeButtons() {
        // ========== 按钮1 (GPIO0)：单击重启，长按配网 ==========
        button1_.OnClick([this]() {
            // 如果长按已触发，忽略单击
            if (button1_long_press_triggered_) {
                button1_long_press_triggered_ = false;
                return;
            }
            
            ESP_LOGI(TAG, "Button1 clicked, restarting device...");
            esp_restart();
        });
        
        button1_.OnLongPress([this]() {
            // 设置长按标志，阻止后续单击
            button1_long_press_triggered_ = true;
            
            auto& app = Application::GetInstance();
            auto state = app.GetDeviceState();
            
            ESP_LOGI(TAG, "Button1 long pressed (WiFi config), state=%d", (int)state);
            
            // 在启动状态、待命状态或WiFi配置状态下，长按进入WiFi热点配网
            if (state == kDeviceStateStarting || state == kDeviceStateIdle || state == kDeviceStateWifiConfiguring) {
                EnterWifiConfigMode();
                return;
            }
            
            ESP_LOGI(TAG, "Button1 long pressed in state %d, ignored", (int)state);
        });
        
        // 长按说话模式：按下开始监听，松开停止
        button1_.OnPressDown([this]() {
            if (press_to_talk_tool_ && press_to_talk_tool_->IsPressToTalkEnabled()) {
                ESP_LOGI(TAG, "Button1 pressed, start listening...");
                Application::GetInstance().StartListening();
                
                // 开花表示正在说话
                MechanicalFlowerController::GetInstance().OpenFlower();
            }
        });
        
        button1_.OnPressUp([this]() {
            if (press_to_talk_tool_ && press_to_talk_tool_->IsPressToTalkEnabled()) {
                ESP_LOGI(TAG, "Button1 released, stop listening...");
                Application::GetInstance().StopListening();
            }
        });
        
        // ========== 按钮2 (GPIO1)：单击打断对话 ==========
        button2_.OnClick([this]() {
            auto& app = Application::GetInstance();
            auto state = app.GetDeviceState();
            
            ESP_LOGI(TAG, "Button2 clicked (interrupt), state=%d", (int)state);
            
            // 如果正在说话或聆听，打断对话
            if (state == kDeviceStateSpeaking || state == kDeviceStateListening) {
                // 打断AI说话
                app.AbortSpeaking(AbortReason::kAbortReasonNone);
                
                // 设置取消标志，让TTS结束后进入待命
                app.SetCancelAfterSpeaking(true);
                
                // 立即合拢花朵
                MechanicalFlowerController::GetInstance().StopWiggling();
                MechanicalFlowerController::GetInstance().CloseFlower();
                
                // 如果在聆听状态，停止聆听
                if (state == kDeviceStateListening) {
                    app.StopListening();
                }
                
                ESP_LOGI(TAG, "Conversation interrupted, flower closed");
            }
        });
        
        // ========== 按钮3 (GPIO12)：暂时闲置 ==========
        // 未来可扩展功能
        button3_.OnClick([this]() {
            ESP_LOGI(TAG, "Button3 clicked (reserved)");
            // 暂时闲置，未来可扩展
        });
        
        button3_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Button3 long pressed (reserved)");
            // 暂时闲置，未来可扩展
        });
        
        ESP_LOGI(TAG, "Buttons initialized:");
        ESP_LOGI(TAG, "  Button1 (GPIO%d): Click=Restart, LongPress=WiFi", BUTTON1_GPIO);
        ESP_LOGI(TAG, "  Button2 (GPIO%d): Click=Interrupt", BUTTON2_GPIO);
        ESP_LOGI(TAG, "  Button3 (GPIO%d): Reserved", BUTTON3_GPIO);
    }
    
    void InitializeFlowerController() {
        // 初始化舵机控制器
        MechanicalFlowerController::GetInstance().Initialize(SERVO_GPIO);
        ESP_LOGI(TAG, "Flower controller initialized on GPIO %d", SERVO_GPIO);
    }
    
    void InitializeLedController() {
        // 初始化LED控制器（包含MCP工具注册）
        FlowerLedController::GetInstance().Initialize(WS2812_GPIO, LED_STRIP_COUNT);
        ESP_LOGI(TAG, "LED controller initialized on GPIO %d with %d LEDs", WS2812_GPIO, LED_STRIP_COUNT);
    }
    
    void InitializeTools() {
        // 初始化一问一答模式工具
        press_to_talk_tool_ = new PressToTalkMcpTool();
        press_to_talk_tool_->Initialize();
        ESP_LOGI(TAG, "Press-to-talk tool initialized");
    }

public:
    MechanicalFlowerBoard() : WifiBoard(), 
        button1_(BUTTON1_GPIO, false, LONG_PRESS_TIME_MS),  // 低电平触发，1秒长按
        button2_(BUTTON2_GPIO, false, LONG_PRESS_TIME_MS),  // 低电平触发
        button3_(BUTTON3_GPIO, false, LONG_PRESS_TIME_MS) { // 低电平触发
        InitializeFlowerController();
        InitializeLedController();
        InitializeTools();
        InitializeButtons();
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Mechanical Flower Board initialized");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Microphone: INMP441 (GPIO3, GPIO2, GPIO10)");
        ESP_LOGI(TAG, "Amplifier: MAX98357A (GPIO13, shared BCLK/WS)");
        ESP_LOGI(TAG, "Servo: SG90 on GPIO %d", SERVO_GPIO);
        ESP_LOGI(TAG, "LED Strip: WS2812 on GPIO %d (%d LEDs)", WS2812_GPIO, LED_STRIP_COUNT);
        ESP_LOGI(TAG, "Button1: GPIO %d (WiFi/Restart)", BUTTON1_GPIO);
        ESP_LOGI(TAG, "Button2: GPIO %d (Interrupt)", BUTTON2_GPIO);
        ESP_LOGI(TAG, "Button3: GPIO %d (Water/QueryLastWaterTime)", BUTTON3_GPIO);
        ESP_LOGI(TAG, "========================================");
    }

    virtual Led* GetLed() override {
        return FlowerLedController::GetInstance().GetLedStrip();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static MechanicalFlowerAudioCodec audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN,
            AUDIO_I2S_AMP_GPIO_DOUT  // MAX98357A功放数据输出引脚
        );
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        static NoDisplay display;
        return &display;
    }
};

DECLARE_BOARD(MechanicalFlowerBoard);