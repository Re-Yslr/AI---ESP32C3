/**
 * @file servo_controller.cc
 * @brief 舵机控制器实现文件
 * 
 * SG90舵机控制 - 使用ESP32 LEDC PWM
 * 初始角度为0°（合拢状态）
 * 角度范围：0°（合拢）~ 48°（开花）
 * - 角度增大 = 顺时针旋转 = 开花（花瓣展开）
 * - 角度减小 = 逆时针旋转 = 合拢（花瓣收起）
 */

#include "servo_controller.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "ServoController"

// PWM参数
// ESP32-C3 LEDC 对低频率有限制，需要使用较低分辨率
// 50Hz + 14位分辨率可以正常工作
#define SERVO_PWM_FREQ          50      // 50Hz
#define SERVO_PWM_RESOLUTION    14      // 14位分辨率 (ESP32-C3兼容)
#define SERVO_MIN_PULSE_US      500     // 最小脉宽 500μs
#define SERVO_MAX_PULSE_US      2500    // 最大脉宽 2500μs
#define SERVO_PERIOD_US         20000   // 周期 20ms

ServoController::ServoController(int gpio, ledc_channel_t channel)
    : gpio_(gpio), channel_(channel), current_angle_(0), initialized_(false) {
    
    // 配置LEDC定时器
    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_conf.duty_resolution = (ledc_timer_bit_t)SERVO_PWM_RESOLUTION;
    timer_conf.timer_num = LEDC_TIMER_0;
    timer_conf.freq_hz = SERVO_PWM_FREQ;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;
    
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config LEDC timer: %s", esp_err_to_name(ret));
        return;
    }
    
    // 配置LEDC通道
    ledc_channel_config_t channel_conf = {};
    channel_conf.gpio_num = gpio_;
    channel_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    channel_conf.channel = channel_;
    channel_conf.intr_type = LEDC_INTR_DISABLE;
    channel_conf.timer_sel = LEDC_TIMER_0;
    channel_conf.duty = 0;  // 初始化时不输出PWM，避免舵机跳变
    channel_conf.hpoint = 0;
    
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config LEDC channel: %s", esp_err_to_name(ret));
        return;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "Servo initialized on GPIO %d, angle: %d° (stored), range: 0°(closed) ~ %d°(open)", 
             gpio_, current_angle_, SERVO_MAX_OPEN_ANGLE);
}

ServoController::~ServoController() {
    if (initialized_) {
        ledc_stop(LEDC_LOW_SPEED_MODE, channel_, 0);
    }
}

int ServoController::ClampAngle(int angle) {
    // 限制角度范围
    // 逆时针方向（角度减小）最大为0°（完全开花）
    // 顺时针方向（角度增大）最大为48°（完全合拢）
    
    if (angle < 0) {
        ESP_LOGW(TAG, "Angle %d° below 0°, clamping to 0°", angle);
        angle = 0;
    }
    
    if (angle > SERVO_MAX_OPEN_ANGLE) {
        ESP_LOGW(TAG, "Angle %d° exceeds max %d°, clamping", angle, SERVO_MAX_OPEN_ANGLE);
        angle = SERVO_MAX_OPEN_ANGLE;
    }
    
    return angle;
}

void ServoController::SetAngle(int angle) {
    if (!initialized_) return;
    
    // 应用角度限制
    angle = ClampAngle(angle);
    
    current_angle_ = angle;
    uint32_t duty = AngleToDuty(angle);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_);
}

void ServoController::SetAngleWithoutOutput(int angle) {
    // 仅更新内部状态，不输出PWM
    // 用于初始化时同步舵机实际位置
    angle = ClampAngle(angle);
    current_angle_ = angle;
    ESP_LOGI(TAG, "Angle set to %d° without output (sync state)", angle);
}

void ServoController::MoveTo(int target_angle, int duration_ms) {
    if (!initialized_) return;
    
    // 应用角度限制
    target_angle = ClampAngle(target_angle);
    
    if (target_angle == current_angle_) return;
    
    int start_angle = current_angle_;
    int steps = abs(target_angle - start_angle);
    int delay_per_step = duration_ms / steps;
    
    // 最小延时15ms，实现更平滑的移动
    if (delay_per_step < 15) delay_per_step = 15;
    
    int direction = (target_angle > start_angle) ? 1 : -1;
    
    for (int i = 0; i <= steps; i++) {
        int angle = start_angle + direction * i;
        // 移动过程中也要限制角度
        angle = ClampAngle(angle);
        SetAngle(angle);
        vTaskDelay(pdMS_TO_TICKS(delay_per_step));
    }
    
    ESP_LOGI(TAG, "Moved from %d° to %d° in %dms", start_angle, target_angle, duration_ms);
}

uint32_t ServoController::AngleToDuty(int angle) {
    // 计算脉宽 (500-2500μs 对应 0-180°)
    int pulse_us = SERVO_MIN_PULSE_US + 
                   (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle / 180;
    
    // 转换为占空比 (14位分辨率)
    // duty = (pulse_us / period_us) * max_duty
    uint32_t duty = (pulse_us * ((1 << SERVO_PWM_RESOLUTION) - 1)) / SERVO_PERIOD_US;
    
    return duty;
}
