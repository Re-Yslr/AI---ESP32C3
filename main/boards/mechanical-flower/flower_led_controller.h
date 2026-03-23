/**
 * @file flower_led_controller.h
 * @brief 机械花LED灯带控制器 - 颜色控制和状态效果
 */

#ifndef FLOWER_LED_CONTROLLER_H
#define FLOWER_LED_CONTROLLER_H

#include "flower_led.h"
#include "mcp_server.h"
#include "settings.h"
#include <memory>

/**
 * @class FlowerLedController
 * @brief 机械花LED控制器
 * 
 * 功能：
 * - 状态灯光效果（聆听时呼吸，待命时常亮）
 * - 颜色控制 MCP 工具
 */
class FlowerLedController {
public:
    static FlowerLedController& GetInstance();
    
    /**
     * @brief 初始化控制器
     * @param gpio LED灯带数据引脚
     * @param led_count LED数量
     */
    void Initialize(gpio_num_t gpio, uint16_t led_count);
    
    /**
     * @brief 获取LED灯带实例
     */
    FlowerLed* GetLedStrip() { return led_strip_; }
    
    /**
     * @brief 设置颜色
     * @param red 红色 (0-255)
     * @param green 绿色 (0-255)
     * @param blue 蓝色 (0-255)
     */
    void SetColor(uint8_t red, uint8_t green, uint8_t blue);
    
    /**
     * @brief 设置预设颜色
     * @param color_name 颜色名称
     */
    bool SetPresetColor(const std::string& color_name);
    
    /**
     * @brief 获取当前颜色
     */
    StripColor GetCurrentColor() const { return current_color_; }

private:
    FlowerLedController();
    ~FlowerLedController();
    
    // 禁止拷贝
    FlowerLedController(const FlowerLedController&) = delete;
    FlowerLedController& operator=(const FlowerLedController&) = delete;
    
    /**
     * @brief 注册MCP工具
     */
    void RegisterMcpTools();
    
    FlowerLed* led_strip_ = nullptr;
    StripColor current_color_;
    bool initialized_ = false;
};

// 预设颜色结构体
struct FlowerLedColorPreset {
    const char* name;
    uint8_t red, green, blue;
};

// 预设颜色表（在.cc文件中定义）
extern const FlowerLedColorPreset flower_led_color_presets[];
extern const int flower_led_color_preset_count;

#endif // FLOWER_LED_CONTROLLER_H
