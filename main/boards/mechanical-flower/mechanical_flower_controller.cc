/**
 * @file mechanical_flower_controller.cc
 * @brief 机械AI仿生花控制器实现
 * 
 * 舵机控制逻辑：
 * - 合拢位置：0°（花瓣收起）
 * - 开花位置：48°（顺时针旋转，花瓣展开）
 * - 摇摆：以48°为中心，向逆时针方向（角度减小）平滑摆动
 * - 开机默认：合拢状态（0°，用户手动调节机械结构）
 */

#include "mechanical_flower_controller.h"
#include "application.h"
#include "device_state_machine.h"
#include "config.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ctime>

#define TAG "FlowerController"

MechanicalFlowerController& MechanicalFlowerController::GetInstance() {
    static MechanicalFlowerController instance;
    return instance;
}

MechanicalFlowerController::MechanicalFlowerController()
    : servo_(nullptr), 
      open_angle_(SERVO_OPEN_ANGLE),      // 48°（开花位置）
      close_angle_(SERVO_CLOSE_ANGLE),    // 0°（合拢位置）
      initialized_(false), 
      state_listener_id_(-1), 
      wiggling_(false), 
      wiggle_thread_(nullptr),
      action_in_progress_(false),
      battery_capacity_mah_(2000),        // 默认电池容量 2000mAh
      battery_remaining_mah_(2000.0f),    // 初始满电
      last_state_change_time_(0),
      current_tracked_state_(kDeviceStateUnknown),
      last_charge_time_(0) {
}

MechanicalFlowerController::~MechanicalFlowerController() {
    StopWiggling();
    
    // 慢速合拢花朵（重启时的优雅关闭）
    if (initialized_ && servo_) {
        ESP_LOGI(TAG, "Graceful shutdown: closing flower slowly");
        servo_->MoveTo(close_angle_, 2000);  // 慢速合拢，与正常合拢速度一致
    }
    
    // 移除状态监听器
    if (state_listener_id_ >= 0) {
        auto& state_machine = Application::GetInstance().GetDeviceStateMachine();
        state_machine.RemoveStateChangeListener(state_listener_id_);
        state_listener_id_ = -1;
    }
}

void MechanicalFlowerController::Initialize(int servo_gpio) {
    if (initialized_) return;
    
    // 从设置中读取角度配置（可被用户覆盖）
    Settings settings("flower", false);
    open_angle_ = settings.GetInt("open_angle", SERVO_OPEN_ANGLE);    // 默认48°（开花位置）
    close_angle_ = settings.GetInt("close_angle", SERVO_CLOSE_ANGLE); // 默认0°（合拢位置）
    
    // 读取上次保存的舵机角度（用于重启时恢复位置）
    int saved_angle = settings.GetInt("current_angle", close_angle_);  // 默认为合拢位置
    
    // 读取电量追踪配置
    // 电量以 mAh 为单位，存储时乘以 100 以提高精度（避免使用 float）
    battery_capacity_mah_ = settings.GetInt("battery_capacity_mah", 2000);
    int remaining_mah_x100 = settings.GetInt("battery_remaining_mah_x100", battery_capacity_mah_ * 100);
    battery_remaining_mah_ = remaining_mah_x100 / 100.0f;
    last_charge_time_ = (time_t)settings.GetInt("last_charge_time", 0);
    
    // 初始化状态追踪
    last_state_change_time_ = time(nullptr);
    current_tracked_state_ = kDeviceStateStarting;
    
    // 创建舵机控制器（初始化时不输出PWM）
    servo_ = std::make_unique<ServoController>(servo_gpio);
    
    // 设置舵机的内部状态为保存的角度（不移动）
    servo_->SetAngleWithoutOutput(saved_angle);
    ESP_LOGI(TAG, "Servo restored to saved angle: %d°", saved_angle);
    
    // 如果保存的角度不是合拢位置，慢速移动到合拢位置
    if (saved_angle != close_angle_) {
        ESP_LOGI(TAG, "Moving to close position slowly...");
        servo_->MoveTo(close_angle_, 2000);  // 慢速合拢，2秒
    }
    
    // 注册状态变化监听器
    auto& state_machine = Application::GetInstance().GetDeviceStateMachine();
    state_listener_id_ = state_machine.AddStateChangeListener(
        [this](DeviceState old_state, DeviceState new_state) {
            OnStateChanged(old_state, new_state);
        });
    
    // 注册MCP工具
    RegisterMcpTools();
    
    initialized_ = true;
    ESP_LOGI(TAG, "Flower controller initialized, open=%d° (顺时针), close=%d° (逆时针)", 
             open_angle_, close_angle_);
    ESP_LOGI(TAG, "Battery: %d mAh capacity, %.1f mAh remaining (%.0f%%)", 
             battery_capacity_mah_, battery_remaining_mah_, 
             battery_remaining_mah_ / battery_capacity_mah_ * 100.0f);
}

void MechanicalFlowerController::OnStateChanged(DeviceState old_state, DeviceState new_state) {
    ESP_LOGI(TAG, "State changed: %d -> %d", (int)old_state, (int)new_state);
    
    // 更新电量消耗和追踪状态（合并到一个锁作用域内，避免死锁）
    {
        std::lock_guard<std::mutex> lock(battery_mutex_);
        
        // 计算旧状态的电量消耗
        if (old_state != kDeviceStateUnknown) {
            time_t now = time(nullptr);
            long duration_seconds = now - last_state_change_time_;
            
            if (duration_seconds > 0) {
                int current_ma = GetStatePowerConsumption(old_state);
                float consumed_mah = (float)(current_ma * duration_seconds) / 3600.0f;
                battery_remaining_mah_ -= consumed_mah;
                if (battery_remaining_mah_ < 0) battery_remaining_mah_ = 0;
                
                ESP_LOGD(TAG, "Battery: %.2f mAh consumed in %ld sec", consumed_mah, duration_seconds);
            }
        }
        
        // 更新追踪状态和时间
        current_tracked_state_ = new_state;
        last_state_change_time_ = time(nullptr);
    }
    
    // 处理舵机操作
    servo_mutex_.lock();
    
    switch (new_state) {
        case kDeviceStateIdle:
            // 待命状态：合拢花朵，停止摇摆
            StopWigglingInternal();
            servo_mutex_.unlock();
            JoinWiggleThread();
            servo_mutex_.lock();
            CloseFlowerInternal();
            break;
            
        case kDeviceStateListening:
            // 仅在聆听状态时开花（不包括连接状态）
            StopWigglingInternal();
            servo_mutex_.unlock();
            JoinWiggleThread();
            servo_mutex_.lock();
            OpenFlowerInternal();
            break;
            
        case kDeviceStateSpeaking:
            // AI说话状态：开始平滑摇摆
            StartWigglingInternal();
            break;
            
        case kDeviceStateConnecting:
            // 连接状态：不做任何动作，保持当前状态
            break;
            
        default:
            StopWigglingInternal();
            servo_mutex_.unlock();
            JoinWiggleThread();
            servo_mutex_.lock();
            break;
    }
    
    servo_mutex_.unlock();
}

void MechanicalFlowerController::OpenFlower() {
    if (!initialized_ || !servo_) return;
    
    std::lock_guard<std::mutex> lock(servo_mutex_);
    OpenFlowerInternal();
}

void MechanicalFlowerController::OpenFlowerInternal() {
    if (!initialized_ || !servo_) return;
    
    ESP_LOGI(TAG, "Opening flower (clockwise to %d°)...", open_angle_);
    servo_->MoveTo(open_angle_, 2000);  // 顺时针旋转到开花位置（48°），慢速移动
    
    // 保存当前角度到Settings（用于重启时恢复位置）
    Settings settings("flower", true);
    settings.SetInt("current_angle", open_angle_);
}

void MechanicalFlowerController::CloseFlower() {
    if (!initialized_ || !servo_) return;
    
    std::lock_guard<std::mutex> lock(servo_mutex_);
    CloseFlowerInternal();
}

void MechanicalFlowerController::CloseFlowerInternal() {
    if (!initialized_ || !servo_) return;
    
    ESP_LOGI(TAG, "Closing flower (counterclockwise to %d°)...", close_angle_);
    servo_->MoveTo(close_angle_, 2000);  // 逆时针旋转到合拢位置（0°），慢速移动
    
    // 保存当前角度到Settings（用于重启时恢复位置）
    Settings settings("flower", true);
    settings.SetInt("current_angle", close_angle_);
}

void MechanicalFlowerController::SetAngle(int angle) {
    if (!initialized_ || !servo_) return;
    std::lock_guard<std::mutex> lock(servo_mutex_);
    servo_->SetAngle(angle);
}

void MechanicalFlowerController::MoveTo(int angle, int duration_ms) {
    if (!initialized_ || !servo_) return;
    std::lock_guard<std::mutex> lock(servo_mutex_);
    servo_->MoveTo(angle, duration_ms);
}

void MechanicalFlowerController::Wiggle(int times) {
    if (!initialized_ || !servo_) return;
    
    std::lock_guard<std::mutex> lock(servo_mutex_);
    
    ESP_LOGI(TAG, "Wiggling %d times...", times);
    int current = servo_->GetAngle();
    
    for (int i = 0; i < times; i++) {
        // 向逆时针方向摇摆（角度减小，趋向合拢）
        servo_->MoveTo(current - 15, 200);
        vTaskDelay(pdMS_TO_TICKS(200));
        servo_->MoveTo(current, 200);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void MechanicalFlowerController::StartWiggling() {
    std::lock_guard<std::mutex> lock(servo_mutex_);
    StartWigglingInternal();
}

void MechanicalFlowerController::StartWigglingInternal() {
    if (wiggling_) return;
    
    wiggling_ = true;
    ESP_LOGI(TAG, "Start smooth wiggling (counterclockwise direction from open position)...");
    
    wiggle_thread_ = new std::thread([this]() {
        int base_angle = open_angle_;  // 以开花角度（48°）为中心
        while (wiggling_) {
            {
                std::lock_guard<std::mutex> lock(servo_mutex_);
                // 向逆时针方向平滑摇摆（角度减小，趋向合拢）
                // 摇摆范围：48° → 33° → 48°（±15°，共30°摆幅）
                if (servo_) {
                    servo_->MoveTo(base_angle - 15, 400);  // 放慢速度，每步400ms
                }
            }
            vTaskDelay(pdMS_TO_TICKS(400));
            if (!wiggling_) break;
            {
                std::lock_guard<std::mutex> lock(servo_mutex_);
                if (servo_) {
                    servo_->MoveTo(base_angle, 400);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(400));
        }
    });
}

void MechanicalFlowerController::StopWiggling() {
    servo_mutex_.lock();
    StopWigglingInternal();
    servo_mutex_.unlock();
    JoinWiggleThread();
}

void MechanicalFlowerController::StopWigglingInternal() {
    if (!wiggling_) return;
    
    wiggling_ = false;
    ESP_LOGI(TAG, "Stop wiggling...");
    
    // 注意：此函数在持有 servo_mutex_ 时被调用
    // 但需要释放锁来 join 线程，所以需要调用者配合
    // 调用者应该释放锁后再调用 JoinWiggleThread
}

void MechanicalFlowerController::JoinWiggleThread() {
    // 此函数在不持有锁的情况下调用
    if (wiggle_thread_ && wiggle_thread_->joinable()) {
        wiggle_thread_->join();
        delete wiggle_thread_;
        wiggle_thread_ = nullptr;
    }
}

int MechanicalFlowerController::GetAngle() const {
    if (!initialized_ || !servo_) return 0;
    return servo_->GetAngle();
}

std::string MechanicalFlowerController::WaterFlower() {
    if (!initialized_ || !servo_) {
        return "花朵控制器未初始化";
    }
    
    // 检查是否有动作正在执行
    {
        std::lock_guard<std::mutex> lock(servo_mutex_);
        if (action_in_progress_) {
            ESP_LOGW(TAG, "Action already in progress, skip watering");
            return "请稍等，小花正在做其他动作";
        }
        action_in_progress_ = true;
        StopWigglingInternal();
    }
    JoinWiggleThread();  // 在释放锁后 join
    
    ESP_LOGI(TAG, "Watering flower...");
    
    // 确保在开花位置（48°）- 持锁时间短
    {
        std::lock_guard<std::mutex> lock(servo_mutex_);
        servo_->MoveTo(open_angle_, 500);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 小幅摆动模拟浇水效果
    // 向逆时针方向摆动（角度减小，趋向合拢），再回来
    for (int i = 0; i < 2; i++) {
        {
            std::lock_guard<std::mutex> lock(servo_mutex_);
            servo_->MoveTo(open_angle_ - 10, 300);  // 向逆时针摆动
        }
        vTaskDelay(pdMS_TO_TICKS(300));
        {
            std::lock_guard<std::mutex> lock(servo_mutex_);
            servo_->MoveTo(open_angle_, 300);       // 回到开花位置
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    
    // 回到开花位置
    {
        std::lock_guard<std::mutex> lock(servo_mutex_);
        servo_->MoveTo(open_angle_, 500);
        action_in_progress_ = false;
    }
    
    // 记录浇水时间
    Settings settings("flower", true);
    time_t now = time(nullptr);
    settings.SetInt("last_water_time", (int32_t)now);
    ESP_LOGI(TAG, "Watering done, time recorded: %ld", (long)now);
    
    return "好的，我来给你浇点水💧";
}

std::string MechanicalFlowerController::GetLastWaterTime() {
    Settings settings("flower", false);
    time_t last_water_time = (time_t)settings.GetInt("last_water_time", 0);
    
    ESP_LOGI(TAG, "GetLastWaterTime: %ld", (long)last_water_time);
    
    if (last_water_time == 0) {
        // 从未浇过水
        return "你还从未给我浇过水呢，我们刚认识呀~";
    }
    
    // 计算时间差
    time_t now = time(nullptr);
    long diff_seconds = now - last_water_time;
    
    // 格式化时间差
    std::string time_str;
    if (diff_seconds < 60) {
        time_str = "刚刚";
    } else if (diff_seconds < 3600) {
        int minutes = diff_seconds / 60;
        time_str = std::to_string(minutes) + "分钟前";
    } else if (diff_seconds < 86400) {
        int hours = diff_seconds / 3600;
        time_str = std::to_string(hours) + "小时前";
    } else {
        int days = diff_seconds / 86400;
        time_str = std::to_string(days) + "天前";
    }
    
    return "上次浇水是在" + time_str + "哦~";
}

int MechanicalFlowerController::GetBatteryLevel() {
    // 计算电量百分比（在同一个锁内完成，避免死锁）
    std::lock_guard<std::mutex> lock(battery_mutex_);
    
    // 更新当前状态的电量消耗
    time_t now = time(nullptr);
    long duration_seconds = now - last_state_change_time_;
    if (duration_seconds > 0 && current_tracked_state_ != kDeviceStateUnknown) {
        int current_ma = GetStatePowerConsumption(current_tracked_state_);
        float consumed_mah = (float)(current_ma * duration_seconds) / 3600.0f;
        battery_remaining_mah_ -= consumed_mah;
        if (battery_remaining_mah_ < 0) battery_remaining_mah_ = 0;
        last_state_change_time_ = now;
    }
    
    int percentage = (int)(battery_remaining_mah_ / battery_capacity_mah_ * 100.0f);
    
    // 限制范围 0-100
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;
    
    ESP_LOGI(TAG, "Battery level: %d%% (%.1f/%d mAh)", 
             percentage, battery_remaining_mah_, battery_capacity_mah_);
    
    return percentage;
}

void MechanicalFlowerController::MarkBatteryCharged() {
    std::lock_guard<std::mutex> lock(battery_mutex_);
    
    battery_remaining_mah_ = (float)battery_capacity_mah_;
    last_charge_time_ = time(nullptr);
    current_tracked_state_ = kDeviceStateIdle;
    last_state_change_time_ = time(nullptr);
    
    // 保存到设置（电量乘以100存储以保持精度）
    Settings settings("flower", true);
    settings.SetInt("battery_remaining_mah_x100", (int32_t)(battery_remaining_mah_ * 100));
    settings.SetInt("last_charge_time", (int32_t)last_charge_time_);
    
    ESP_LOGI(TAG, "Battery marked as fully charged: %d mAh", battery_capacity_mah_);
}

int MechanicalFlowerController::GetStatePowerConsumption(DeviceState state) {
    // 细化功耗计算模型
    // 基于 2000mAh 电池，2W 8Ω 扬声器，3颗 WS2812 LED
    
    // 基础功耗组件：
    // - ESP32-C3 基础: ~20mA（恒定）
    // - WS2812 LED (3颗): 亮度% × 60mA（每颗20mA@满亮）
    // - INMP441 麦克风: ~1.4mA（聆听时）
    // - MAX98357A + 2W 8Ω扬声器: 音量% × 150mA（峰值约150mA@5V）
    // - SG90 舵机: 动作时 ~200mA，静止时 ~5mA
    // - WiFi: 活跃时 ~100mA，空闲时 ~20mA
    
    // 典型场景估算（使用保守值）：
    // - LED 亮度: 待命时50%，聆听/说话时呼吸效果约30%平均
    // - 扬声器音量: 说话时平均约50%
    // - 舵机: 说话时摇摆，其他时候静止
    
    switch (state) {
        case kDeviceStateIdle:
            // 待命: ESP32(20) + LED常亮50%(30) + 舵机静止(5) + WiFi空闲(20)
            return 75;
            
        case kDeviceStateListening:
            // 聆听: ESP32(20) + LED呼吸30%(18) + 麦克风(1.4) + WiFi活跃(100) + 舵机静止(5)
            return 145;
            
        case kDeviceStateSpeaking:
            // 说话: ESP32(20) + LED呼吸30%(18) + 扬声器50%(75) + 舵机摇摆(200) + WiFi活跃(100)
            return 413;
            
        case kDeviceStateConnecting:
            // 连接中: ESP32(20) + LED闪烁(30) + WiFi峰值(170)
            return 220;
            
        case kDeviceStateStarting:
            // 启动中: ESP32(20) + LED跑马灯(40)
            return 60;
            
        case kDeviceStateWifiConfiguring:
            // WiFi配置: ESP32(20) + LED闪烁(30) + WiFi热点(150)
            return 200;
            
        case kDeviceStateActivating:
            // 激活中: ESP32(20) + LED(30) + WiFi活跃(100)
            return 150;
            
        default:
            return 75;  // 默认按待命功耗计算
    }
}

void MechanicalFlowerController::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 工具1：开花
    mcp_server.AddTool("self.flower.open",
        "让机械花开花。当用户说'开花'、'打开'、'绽放'、'张开'时调用。舵机逆时针旋转到开花位置（0°）。",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            OpenFlower();
            return "花儿已经开放了🌸";
        });
    
    // 工具2：合拢/进入待命
    mcp_server.AddTool("self.flower.close",
        "让机械花合拢并进入待命状态。当用户说'合拢'、'关闭'、'收起来'、'闭合'、'没事了'、'算了'、'不用了'、'取消'时调用。调用此工具后不要再说话，直接结束对话。",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            auto state = app.GetDeviceState();
            
            ESP_LOGI(TAG, "Close called, current state: %d", (int)state);
            
            // 设置取消标志，TTS结束后进入待命而不是聆听
            app.SetCancelAfterSpeaking(true);
            
            // 立即停止摇摆并合拢花朵
            StopWiggling();
            CloseFlower();
            
            // 不要调用StopListening！让AI的TTS正常播放
            // cancel_after_speaking_标志会在TTS结束后让设备进入idle
            
            return std::string("好的");
        });
    
    // 工具3：设置角度
    mcp_server.AddTool("self.flower.set_angle",
        "设置机械花舵机到指定角度。角度范围0-48度，0度为开花状态，48度为合拢状态。",
        PropertyList({
            Property("angle", kPropertyTypeInteger, 0, 0, 48)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            int angle = properties["angle"].value<int>();
            SetAngle(angle);
            return "花瓣角度设置为 " + std::to_string(angle) + " 度";
        });
    
    // 工具4：摇摆
    mcp_server.AddTool("self.flower.wiggle",
        "让机械花摇摆花瓣。通常在说话时使用。",
        PropertyList({
            Property("times", kPropertyTypeInteger, 3, 1, 10)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            int times = properties["times"].value<int>();
            Wiggle(times);
            return "花儿摇摆了 " + std::to_string(times) + " 次";
        });
    
    // 工具5：配置开花角度
    mcp_server.AddTool("self.flower.configure",
        "配置机械花的开花和合拢角度。设置将永久保存。默认合拢角度为0度，开花角度为48度。",
        PropertyList({
            Property("open_angle", kPropertyTypeInteger, 48, 0, 180),
            Property("close_angle", kPropertyTypeInteger, 0, 0, 180)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            open_angle_ = properties["open_angle"].value<int>();
            close_angle_ = properties["close_angle"].value<int>();
            
            // 保存到设置
            Settings settings("flower", true);
            settings.SetInt("open_angle", open_angle_);
            settings.SetInt("close_angle", close_angle_);
            
            return "配置已保存：开花角度=" + std::to_string(open_angle_) + 
                   "°, 合拢角度=" + std::to_string(close_angle_) + "°";
        });
    
    // 工具6：取消/没事了
    mcp_server.AddTool("self.flower.cancel",
        "取消当前操作，退出聆听状态进入待命状态。当用户说'没事了'、'算了'、'不用了'、'取消'、'停止'、'不用管了'时调用。调用此工具后不要再说话，直接结束对话。",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            auto state = app.GetDeviceState();
            
            ESP_LOGI(TAG, "Cancel called, current state: %d", (int)state);
            
            // 设置取消标志，TTS结束后进入待命而不是聆听
            app.SetCancelAfterSpeaking(true);
            
            // 立即停止摇摆并合拢花朵
            StopWiggling();
            CloseFlower();
            
            // 不要调用StopListening！让AI的TTS正常播放
            // cancel_after_speaking_标志会在TTS结束后让设备进入idle
            
            return std::string("好的");
        });
    
    // 工具7：浇水
    mcp_server.AddTool("self.flower.water",
        "给花浇水。当用户说'浇水'、'给花浇水'、'浇点水'时调用。舵机会做浇水动作。",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            return WaterFlower();
        });
    
    // 工具8：查询上次浇水时间
    mcp_server.AddTool("self.flower.last_water_time",
        "查询上次给花浇水的时间。当用户问'上次什么时候浇水'、'什么时候浇的水'、'上次浇水时间'时调用。",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            return GetLastWaterTime();
        });
    
    // 工具9：查询电量
    mcp_server.AddTool("self.flower.battery",
        "查询电池电量。当用户问'电量'、'还有多少电'、'电池电量'、'现在电量'时调用。",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            int level = GetBatteryLevel();
            return "我现在电量大约" + std::to_string(level) + "%哦~";
        });
    
    // 工具10：标记充满电
    mcp_server.AddTool("self.flower.mark_charged",
        "标记电池已充满电。当用户说'充满电了'、'充好电了'、'电充满了'时调用。重置电量追踪。",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            MarkBatteryCharged();
            return "太好了，我已经充满电啦！现在电量100%哦~";
        });
    
    ESP_LOGI(TAG, "MCP tools registered (10 tools including water/battery)");
}