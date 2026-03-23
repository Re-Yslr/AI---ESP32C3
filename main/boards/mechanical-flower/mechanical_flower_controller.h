/**
 * @file mechanical_flower_controller.h
 * @brief 机械AI仿生花控制器 - 舵机与MCP工具集成
 * 
 * 提供舵机控制（开花/合拢）和AI对话集成功能
 * 
 * 【舵机角度配置】
 * - 合拢位置：0°（花瓣收起）
 * - 开花位置：48°（顺时针旋转，花瓣展开）
 * - 摇摆：以48°为中心，向逆时针方向（角度减小）摆动
 * - 开机默认：合拢状态（0°）
 */

#ifndef MECHANICAL_FLOWER_CONTROLLER_H
#define MECHANICAL_FLOWER_CONTROLLER_H

#include "servo_controller.h"
#include "mcp_server.h"
#include "settings.h"
#include "device_state.h"
#include <memory>
#include <thread>
#include <functional>
#include <mutex>

/**
 * @class MechanicalFlowerController
 * @brief 机械花控制器
 * 
 * 管理舵机运动和MCP工具注册
 */
class MechanicalFlowerController {
public:
    /**
     * @brief 获取单例实例
     */
    static MechanicalFlowerController& GetInstance();
    
    /**
     * @brief 初始化控制器
     * @param servo_gpio 舵机PWM引脚
     */
    void Initialize(int servo_gpio);
    
    /**
     * @brief 开花 - 舵机转到打开位置（0°）
     * 逆时针旋转到最小角度
     */
    void OpenFlower();
    
    /**
     * @brief 合拢 - 舵机转到关闭位置（48°）
     * 顺时针旋转48度
     */
    void CloseFlower();
    
    /**
     * @brief 设置舵机角度
     * @param angle 角度 (0-180)
     */
    void SetAngle(int angle);
    
    /**
     * @brief 平滑移动
     * @param angle 目标角度
     * @param duration_ms 移动时间
     */
    void MoveTo(int angle, int duration_ms = 1000);
    
    /**
     * @brief 摇摆花瓣（AI说话时的动画）
     * @param times 摇摆次数
     */
    void Wiggle(int times = 3);
    
    /**
     * @brief 开始连续摇摆（AI说话时）
     * 向顺时针方向平滑摆动
     */
    void StartWiggling();
    
    /**
     * @brief 停止连续摇摆
     */
    void StopWiggling();
    
    /**
     * @brief 获取当前角度
     */
    int GetAngle() const;
    
    /**
     * @brief 浇水动作
     * 小幅摆动 + 回到开花位置
     * @return AI回复文本
     */
    std::string WaterFlower();
    
    /**
     * @brief 获取上次浇水时间信息
     * @return AI回复文本
     */
    std::string GetLastWaterTime();
    
    /**
     * @brief 获取电池电量
     * @return 电量百分比 (0-100)
     */
    int GetBatteryLevel();
    
    /**
     * @brief 标记电池已充满电
     * 用户说"充满电了"、"充好电了"时调用
     */
    void MarkBatteryCharged();

private:
    MechanicalFlowerController();
    ~MechanicalFlowerController();
    
    // 禁止拷贝
    MechanicalFlowerController(const MechanicalFlowerController&) = delete;
    MechanicalFlowerController& operator=(const MechanicalFlowerController&) = delete;
    
    /**
     * @brief 注册MCP工具
     */
    void RegisterMcpTools();
    
    /**
     * @brief 状态变化回调
     */
    void OnStateChanged(DeviceState old_state, DeviceState new_state);
    
    /**
     * @brief 获取指定状态的功耗（mA）
     */
    int GetStatePowerConsumption(DeviceState state);
    
    /**
     * @brief 内部函数（无锁版本，供已持有锁的函数调用）
     */
    void OpenFlowerInternal();
    void CloseFlowerInternal();
    void StartWigglingInternal();
    void StopWigglingInternal();
    void JoinWiggleThread();  // 在不持有锁时 join 线程
    
    std::unique_ptr<ServoController> servo_;
    int open_angle_;          // 开花角度（默认48°）
    int close_angle_;         // 合拢角度（默认0°）
    bool initialized_;
    int state_listener_id_;   // 状态监听器ID
    bool wiggling_;           // 是否正在摇摆
    std::thread* wiggle_thread_;  // 摇摆线程
    std::mutex servo_mutex_;  // 舵机操作互斥锁
    bool action_in_progress_; // 是否正在执行动作（防止冲突）
    
    // 电量追踪相关
    int battery_capacity_mah_;        // 电池容量 (mAh)，默认2000
    float battery_remaining_mah_;     // 剩余电量 (mAh)
    time_t last_state_change_time_;   // 上次状态变化时间
    DeviceState current_tracked_state_; // 当前追踪的状态
    std::mutex battery_mutex_;        // 电量追踪互斥锁
    time_t last_charge_time_;         // 上次充满电时间
};

#endif // MECHANICAL_FLOWER_CONTROLLER_H
