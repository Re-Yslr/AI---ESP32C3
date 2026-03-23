/**
 * @file mechanical_flower_audio_codec.cc
 * @brief 机械AI仿生花音频编解码器实现文件
 * 
 * 实现INMP441 I2S麦克风的音频采集和MAX98357A I2S功放的音频输出功能
 * 
 * ESP32-C3只有一个I2S控制器，全双工模式下TX和RX必须：
 * 1. 同时创建
 * 2. 使用相同的时钟配置（采样率）
 * 3. 共用BCLK和WS引脚
 * 
 * 由于硬件限制，使用16kHz作为统一采样率
 */

#include "mechanical_flower_audio_codec.h"

#include <esp_log.h>
#include <cmath>
#include <cstring>

#define TAG "MechanicalFlowerAudio"

// 统一采样率（全双工模式TX和RX必须相同）
#define I2S_SAMPLE_RATE 16000

MechanicalFlowerAudioCodec::MechanicalFlowerAudioCodec(
    int input_sample_rate, int output_sample_rate,
    gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din,
    gpio_num_t amp_dout)
    : mic_sck_(mic_sck), 
      mic_ws_(mic_ws), 
      mic_din_(mic_din), 
      amp_dout_(amp_dout),
      rx_handle_(nullptr),
      tx_handle_(nullptr) {
    
    // 设置音频编解码器属性
    duplex_ = (amp_dout_ != GPIO_NUM_NC);
    input_reference_ = false;
    // 使用统一采样率
    input_sample_rate_ = I2S_SAMPLE_RATE;
    output_sample_rate_ = I2S_SAMPLE_RATE;
    
    // ========== 步骤1：同时创建TX和RX通道（全双工） ==========
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear_after_cb = true;
    chan_cfg.auto_clear_before_cb = false;
    
    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channels: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "I2S channels created in full-duplex mode");
    
    // ========== 步骤2：配置I2S标准模式 ==========
    // 全双工模式下TX和RX必须使用相同配置
    i2s_std_config_t std_cfg = {};
    
    // 时钟配置 - 16kHz
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE);
    
    // 插槽配置 - 16位数据，单声道左声道
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    
    // GPIO配置
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = mic_sck_;
    std_cfg.gpio_cfg.ws = mic_ws_;
    std_cfg.gpio_cfg.dout = amp_dout_;
    std_cfg.gpio_cfg.din = mic_din_;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;
    
    // 初始化TX通道
    ret = i2s_channel_init_std_mode(tx_handle_, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init TX channel: %s", esp_err_to_name(ret));
        return;
    }
    
    // 初始化RX通道（使用相同配置）
    ret = i2s_channel_init_std_mode(rx_handle_, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init RX channel: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Audio Codec initialized (Full-Duplex)");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Sample rate: %d Hz (TX/RX shared)", I2S_SAMPLE_RATE);
    ESP_LOGI(TAG, "Data width: 16-bit");
    ESP_LOGI(TAG, "BCLK: GPIO%d, WS: GPIO%d", mic_sck_, mic_ws_);
    ESP_LOGI(TAG, "DIN: GPIO%d (mic), DOUT: GPIO%d (amp)", mic_din_, amp_dout_);
    ESP_LOGI(TAG, "========================================");
}

MechanicalFlowerAudioCodec::~MechanicalFlowerAudioCodec() {
    if (rx_handle_ != nullptr) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
    }
    if (tx_handle_ != nullptr) {
        i2s_channel_disable(tx_handle_);
        i2s_del_channel(tx_handle_);
    }
}

int MechanicalFlowerAudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    
    if (!output_enabled_ || tx_handle_ == nullptr) {
        return 0;
    }
    
    // 直接写入原始数据，不做软件音量调整
    // MAX98357A硬件增益配置：GAIN引脚接100K到GND = 15dB最大增益
    size_t bytes_written;
    esp_err_t ret = i2s_channel_write(tx_handle_, data, 
                                       samples * sizeof(int16_t),
                                       &bytes_written, portMAX_DELAY);
    if (ret != ESP_OK) {
        return 0;
    }
    
    return bytes_written / sizeof(int16_t);
}

int MechanicalFlowerAudioCodec::Read(int16_t* dest, int samples) {
    size_t bytes_read;
    constexpr TickType_t kReadTimeoutTicks = pdMS_TO_TICKS(200);

    // 16位数据直接读取
    if (i2s_channel_read(rx_handle_, dest, samples * sizeof(int16_t), 
                         &bytes_read, kReadTimeoutTicks) != ESP_OK) {
        return 0;
    }

    return bytes_read / sizeof(int16_t);
}

void MechanicalFlowerAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    
    if (enable == input_enabled_ || rx_handle_ == nullptr) {
        return;
    }
    
    if (enable) {
        esp_err_t ret = i2s_channel_enable(rx_handle_);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Microphone enabled");
        } else {
            ESP_LOGE(TAG, "Failed to enable microphone: %s", esp_err_to_name(ret));
        }
    } else {
        i2s_channel_disable(rx_handle_);
        ESP_LOGI(TAG, "Microphone disabled");
    }
    
    AudioCodec::EnableInput(enable);
}

void MechanicalFlowerAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    
    if (enable == output_enabled_ || tx_handle_ == nullptr) {
        AudioCodec::EnableOutput(false);
        return;
    }
    
    if (enable) {
        esp_err_t ret = i2s_channel_enable(tx_handle_);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Speaker enabled");
        } else {
            ESP_LOGE(TAG, "Failed to enable speaker: %s", esp_err_to_name(ret));
        }
    } else {
        i2s_channel_disable(tx_handle_);
        ESP_LOGI(TAG, "Speaker disabled");
    }
    
    AudioCodec::EnableOutput(enable);
}
