/**
 * @file flower_led.h
 * @brief 机械花LED灯带 - 自定义状态效果
 * 
 * 状态效果：
 * - 聆听/说话状态：呼吸效果（保持当前颜色）
 * - 待命状态：常亮
 * - 只有用户明确要求变色时才改变颜色
 */

#ifndef FLOWER_LED_H
#define FLOWER_LED_H

#include "led/circular_strip.h"
#include "application.h"
#include "device_state.h"
#include <esp_log.h>

class FlowerLed : public CircularStrip {
public:
    FlowerLed(gpio_num_t gpio, uint16_t max_leds) 
        : CircularStrip(gpio, max_leds) {
        // 默认颜色：温暖粉色
        current_color_.red = 255;
        current_color_.green = 100;
        current_color_.blue = 150;
    }
    
    virtual ~FlowerLed() = default;
    
    /**
     * @brief 设置基础颜色（用户要求变色时调用）
     */
    void SetBaseColor(StripColor color) {
        current_color_ = color;
        // 立即更新显示
        auto state = Application::GetInstance().GetDeviceState();
        if (state == kDeviceStateIdle) {
            SetAllColor(current_color_);
        } else if (state == kDeviceStateListening || state == kDeviceStateSpeaking) {
            // 重新启动呼吸效果以应用新颜色
            StartBreathing();
        }
    }
    
    /**
     * @brief 获取当前颜色
     */
    StripColor GetBaseColor() const { return current_color_; }

    /**
     * @brief 状态变化处理 - 重写父类方法
     */
    void OnStateChanged() override {
        auto& app = Application::GetInstance();
        auto device_state = app.GetDeviceState();
        
        switch (device_state) {
            case kDeviceStateListening:
            case kDeviceStateSpeaking: {
                // 聆听/说话状态：呼吸效果（使用当前颜色）
                StartBreathing();
                ESP_LOGD("FlowerLed", "State %d: breathing effect with saved color", device_state);
                break;
            }
            case kDeviceStateIdle: {
                // 待命状态：常亮（使用当前颜色）
                SetAllColor(current_color_);
                ESP_LOGD("FlowerLed", "Idle: steady light");
                break;
            }
            case kDeviceStateStarting: {
                // 启动：跑马灯
                StripColor low;
                low.red = 0; low.green = 0; low.blue = 0;
                StripColor high;
                high.red = 0; high.green = 50; high.blue = 100;
                Scroll(low, high, 3, 100);
                break;
            }
            case kDeviceStateWifiConfiguring: {
                // WiFi配置：蓝色闪烁
                StripColor color;
                color.red = 0; color.green = 50; color.blue = 200;
                Blink(color, 500);
                break;
            }
            case kDeviceStateConnecting: {
                // 连接中：当前颜色
                SetAllColor(current_color_);
                break;
            }
            default:
                // 其他状态：使用当前颜色
                SetAllColor(current_color_);
                break;
        }
    }

private:
    StripColor current_color_;
    
    /**
     * @brief 启动呼吸效果（使用当前颜色）
     */
    void StartBreathing() {
        // 低亮度：当前颜色的1/10
        StripColor low;
        low.red = current_color_.red / 10;
        low.green = current_color_.green / 10;
        low.blue = current_color_.blue / 10;
        
        // 高亮度：当前颜色
        Breathe(low, current_color_, 20);
    }
};

#endif // FLOWER_LED_H