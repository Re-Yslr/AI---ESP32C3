/**
 * @file servo_controller.h
 * @brief 舵机控制器 - SG90舵机PWM控制
 * 
 * 使用ESP32 LEDC外设生成50Hz PWM信号控制舵机
 * 脉宽范围：500-2500μs 对应 0°-180°
 * 
 * 角度范围与方向：
 * - 0°：开花位置（逆时针最大）
 * - 48°：合拢位置（顺时针最大）
 * - 角度增大 = 顺时针旋转 = 合拢
 * - 角度减小 = 逆时针旋转 = 开花
 */

#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <driver/ledc.h>
#include <esp_log.h>

// 最大角度限制（顺时针方向，合拢位置）
#define SERVO_MAX_OPEN_ANGLE    48

/**
 * @class ServoController
 * @brief SG90舵机控制器
 */
class ServoController {
public:
    /**
     * @brief 构造函数
     * @param gpio PWM信号引脚
     * @param channel LEDC通道
     */
    ServoController(int gpio, ledc_channel_t channel = LEDC_CHANNEL_0);
    
    /**
     * @brief 析构函数
     */
    ~ServoController();
    
    /**
     * @brief 设置舵机角度
     * @param angle 角度 (0-48度)
     * @note 0°为开花位置，48°为合拢位置
     */
    void SetAngle(int angle);
    
    /**
     * @brief 获取当前角度
     * @return 当前角度
     */
    int GetAngle() const { return current_angle_; }
    
    /**
     * @brief 设置当前角度（不输出PWM）
     * @param angle 角度 (0-48度)
     * @note 仅更新内部状态，不移动舵机。用于初始化时同步状态。
     */
    void SetAngleWithoutOutput(int angle);
    
    /**
     * @brief 平滑移动到目标角度
     * @param target_angle 目标角度
     * @param duration_ms 移动时间（毫秒）
     */
    void MoveTo(int target_angle, int duration_ms = 1000);

private:
    /**
     * @brief 角度转换为PWM占空比
     * @param angle 角度
     * @return PWM占空比值
     */
    uint32_t AngleToDuty(int angle);
    
    /**
     * @brief 限制角度范围
     * @param angle 原始角度
     * @return 限制后的角度
     */
    int ClampAngle(int angle);

private:
    int gpio_;                      // PWM引脚
    ledc_channel_t channel_;        // LEDC通道
    int current_angle_;             // 当前角度
    bool initialized_;              // 是否已初始化
};

#endif // SERVO_CONTROLLER_H