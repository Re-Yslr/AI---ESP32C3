/**
 * @file flower_led_controller.cc
 * @brief 机械花LED灯带控制器实现
 */

#include "flower_led_controller.h"
#include "application.h"
#include "device_state.h"
#include <esp_log.h>
#include <random>

#define TAG "FlowerLed"

// 预设颜色表（支持中英文别名）
const FlowerLedColorPreset flower_led_color_presets[] = {
    // 红色系
    {"red",         255,  50,  50},   // 红色
    {"crimson",     220,  20,  60},   // 深红
    {"maroon",      128,   0,   0},   // 栗色
    {"coral",       255, 127,  80},   // 珊瑚色
    {"salmon",      250, 128, 114},   // 鲑鱼色
    
    // 橙色系
    {"orange",      255, 150,  50},   // 橙色
    {"darkorange",  255, 140,   0},   // 深橙
    
    // 黄色系
    {"yellow",      255, 255,  50},   // 黄色
    {"gold",        255, 215,   0},   // 金色
    {"khaki",       240, 230, 140},   // 卡其色
    
    // 绿色系
    {"green",        50, 255,  50},   // 绿色
    {"lime",        200, 255,   0},   // 青柠
    {"forest",       34, 139,  34},   // 森林绿
    {"olive",       128, 128,   0},   // 橄榄绿
    {"mint",        152, 255, 152},   // 薄荷绿
    
    // 青色系
    {"cyan",         50, 255, 255},   // 青色
    {"teal",          0, 128, 128},   // 蓝绿
    {"turquoise",    64, 224, 208},   // 绿松石
    
    // 蓝色系
    {"blue",         50,  50, 255},   // 蓝色
    {"navy",          0,   0, 128},   // 深蓝
    {"sky",         135, 206, 235},   // 天蓝
    {"royal",        65, 105, 225},   // 皇家蓝
    
    // 紫色系
    {"purple",      200,  50, 255},   // 紫色
    {"violet",      238, 130, 238},   // 紫罗兰
    {"magenta",     255,   0, 255},   // 洋红
    {"lavender",    230, 190, 255},   // 薰衣草
    
    // 粉色系
    {"pink",        255, 100, 150},   // 粉色
    {"rose",        255,   0, 127},   // 玫瑰粉
    {"peach",       255, 218, 185},   // 桃色
    
    // 白色系
    {"white",       255, 255, 255},   // 白色
    {"warm",        255, 200, 100},   // 暖白
    {"cool",        200, 200, 255},   // 冷白
    {"ivory",       255, 255, 240},   // 象牙白
};

const int flower_led_color_preset_count = sizeof(flower_led_color_presets) / sizeof(flower_led_color_presets[0]);

FlowerLedController& FlowerLedController::GetInstance() {
    static FlowerLedController instance;
    return instance;
}

FlowerLedController::FlowerLedController() {
    // 默认颜色：温暖粉色
    current_color_.red = 255;
    current_color_.green = 100;
    current_color_.blue = 150;
}

FlowerLedController::~FlowerLedController() {
    if (led_strip_) {
        delete led_strip_;
        led_strip_ = nullptr;
    }
}

void FlowerLedController::Initialize(gpio_num_t gpio, uint16_t led_count) {
    if (initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "初始化LED灯带: GPIO=%d, LED数量=%d", gpio, led_count);
    
    // 使用自定义的 FlowerLed 类
    led_strip_ = new FlowerLed(gpio, led_count);
    led_strip_->SetBrightness(32, 8);  // 默认亮度32，低亮度8
    
    // 恢复保存的颜色
    Settings settings("flower_led");
    int saved_r = settings.GetInt("red", current_color_.red);
    int saved_g = settings.GetInt("green", current_color_.green);
    int saved_b = settings.GetInt("blue", current_color_.blue);
    current_color_.red = static_cast<uint8_t>(saved_r);
    current_color_.green = static_cast<uint8_t>(saved_g);
    current_color_.blue = static_cast<uint8_t>(saved_b);
    led_strip_->SetBaseColor(current_color_);
    
    // 注册MCP工具
    RegisterMcpTools();
    
    initialized_ = true;
    ESP_LOGI(TAG, "LED灯带初始化完成");
}

void FlowerLedController::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 设置颜色工具
    mcp_server.AddTool("self.flower_led.set_color",
        "设置机械花的LED灯带颜色。参数: red(0-255), green(0-255), blue(0-255)",
        PropertyList({
            Property("red", kPropertyTypeInteger, 255, 0, 255),
            Property("green", kPropertyTypeInteger, 100, 0, 255),
            Property("blue", kPropertyTypeInteger, 150, 0, 255)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            
            // 限制范围
            red = (red < 0) ? 0 : (red > 255 ? 255 : red);
            green = (green < 0) ? 0 : (green > 255 ? 255 : green);
            blue = (blue < 0) ? 0 : (blue > 255 ? 255 : blue);
            
            SetColor(red, green, blue);
            
            // 保存设置
            Settings settings("flower_led", true);
            settings.SetInt("red", red);
            settings.SetInt("green", green);
            settings.SetInt("blue", blue);
            
            char response[64];
            snprintf(response, sizeof(response), "已设置LED颜色为RGB(%d, %d, %d)", red, green, blue);
            return std::string(response);
        });
    
    // 更换颜色工具（随机或指定）
    mcp_server.AddTool("self.flower_led.change_color",
        "更换机械花的LED灯带颜色。当用户说'换个颜色'、'变个色'、'换颜色'、'变颜色'、'我不喜欢这个颜色'、'换个色'时调用。可以指定颜色名称或随机更换相近颜色。",
        PropertyList({
            Property("color", kPropertyTypeString, std::string(""))
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            std::string color_name = properties["color"].value<std::string>();
            
            if (!color_name.empty()) {
                if (SetPresetColor(color_name)) {
                    return "已将LED颜色更改为" + color_name;
                } else {
                    // 颜色名称不识别，随机选择
                    ESP_LOGW(TAG, "未识别的颜色名称: %s，将随机选择相近颜色", color_name.c_str());
                }
            }
            
            // 计算与当前颜色相近的颜色（色相偏移）
            // 使用HSL色彩空间计算，选择色相相近的颜色
            int current_r = current_color_.red;
            int current_g = current_color_.green;
            int current_b = current_color_.blue;
            
            // 找出与当前颜色距离最小的几个颜色
            int best_indices[5] = {-1, -1, -1, -1, -1};
            int best_distances[5] = {999999, 999999, 999999, 999999, 999999};
            
            for (int i = 0; i < flower_led_color_preset_count; i++) {
                const auto& preset = flower_led_color_presets[i];
                int dr = preset.red - current_r;
                int dg = preset.green - current_g;
                int db = preset.blue - current_b;
                int distance = dr*dr + dg*dg + db*db;
                
                // 排除当前颜色（距离为0）
                if (distance < 100) continue;
                
                // 插入排序
                for (int j = 0; j < 5; j++) {
                    if (distance < best_distances[j]) {
                        for (int k = 4; k > j; k--) {
                            best_distances[k] = best_distances[k-1];
                            best_indices[k] = best_indices[k-1];
                        }
                        best_distances[j] = distance;
                        best_indices[j] = i;
                        break;
                    }
                }
            }
            
            // 从最相近的5个颜色中随机选一个
            int valid_count = 0;
            for (int i = 0; i < 5; i++) {
                if (best_indices[i] >= 0) valid_count++;
            }
            
            int selected_index;
            if (valid_count > 0) {
                int pick = esp_random() % valid_count;
                selected_index = best_indices[pick];
            } else {
                // 没有相近颜色，完全随机
                selected_index = esp_random() % flower_led_color_preset_count;
            }
            
            const auto& preset = flower_led_color_presets[selected_index];
            SetColor(preset.red, preset.green, preset.blue);
            
            // 保存设置
            Settings settings("flower_led", true);
            settings.SetInt("red", preset.red);
            settings.SetInt("green", preset.green);
            settings.SetInt("blue", preset.blue);
            
            return std::string("已将LED颜色更改为") + preset.name;
        });
    
    // 获取当前颜色
    mcp_server.AddTool("self.flower_led.get_color",
        "获取机械花当前LED灯带颜色",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            char response[64];
            snprintf(response, sizeof(response), "当前LED颜色: RGB(%d, %d, %d)", 
                     current_color_.red, current_color_.green, current_color_.blue);
            return std::string(response);
        });
    
    ESP_LOGI(TAG, "LED MCP工具注册完成");
}

void FlowerLedController::SetColor(uint8_t red, uint8_t green, uint8_t blue) {
    if (!led_strip_) return;
    
    current_color_.red = red;
    current_color_.green = green;
    current_color_.blue = blue;
    
    ESP_LOGI(TAG, "设置颜色: RGB(%d, %d, %d)", red, green, blue);
    
    // 设置基础颜色，FlowerLed 会自动处理呼吸/常亮效果
    led_strip_->SetBaseColor(current_color_);
}

bool FlowerLedController::SetPresetColor(const std::string& color_name) {
    // 中文颜色名称映射
    static const struct {
        const char* chinese;
        const char* english;
    } chinese_color_map[] = {
        {"红", "red"}, {"红色", "red"}, {"深红", "crimson"}, {"栗色", "maroon"}, {"珊瑚", "coral"},
        {"橙", "orange"}, {"橙色", "orange"}, {"深橙", "darkorange"},
        {"黄", "yellow"}, {"黄色", "yellow"}, {"金色", "gold"}, {"卡其", "khaki"},
        {"绿", "green"}, {"绿色", "green"}, {"青柠", "lime"}, {"森林绿", "forest"}, {"橄榄绿", "olive"}, {"薄荷", "mint"},
        {"青", "cyan"}, {"青色", "cyan"}, {"蓝绿", "teal"}, {"绿松石", "turquoise"},
        {"蓝", "blue"}, {"蓝色", "blue"}, {"深蓝", "navy"}, {"天蓝", "sky"}, {"皇家蓝", "royal"},
        {"紫", "purple"}, {"紫色", "purple"}, {"紫罗兰", "violet"}, {"洋红", "magenta"}, {"薰衣草", "lavender"},
        {"粉", "pink"}, {"粉色", "pink"}, {"玫瑰", "rose"}, {"桃色", "peach"},
        {"白", "white"}, {"白色", "white"}, {"暖白", "warm"}, {"冷白", "cool"}, {"象牙白", "ivory"},
    };
    const int chinese_map_size = sizeof(chinese_color_map) / sizeof(chinese_color_map[0]);
    
    std::string search_name = color_name;
    
    // 转换中文到英文
    for (int i = 0; i < chinese_map_size; i++) {
        if (color_name == chinese_color_map[i].chinese) {
            search_name = chinese_color_map[i].english;
            break;
        }
    }
    
    // 查找预设颜色
    for (int i = 0; i < flower_led_color_preset_count; i++) {
        if (strcasecmp(search_name.c_str(), flower_led_color_presets[i].name) == 0) {
            SetColor(flower_led_color_presets[i].red, 
                     flower_led_color_presets[i].green, 
                     flower_led_color_presets[i].blue);
            return true;
        }
    }
    return false;
}