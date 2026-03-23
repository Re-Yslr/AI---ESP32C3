/**
 * @file mechanical_flower_audio_codec.h
 * @brief 机械AI仿生花音频编解码器头文件
 * 
 * 自定义音频编解码器类，专门为机械AI仿生花设计
 * 
 * 【功能说明】
 * - 支持：INMP441 I2S MEMS麦克风输入
 * - 支持：MAX98357A I2S数字功放输出
 * 
 * 【INMP441配置】
 * - I2S标准模式（Philips I2S）
 * - 32位数据位宽，取高16位有效数据
 * - 单声道模式（左声道，L/R引脚接地）
 * - 16kHz采样率
 * 
 * 【MAX98357A配置】
 * - I2S标准模式
 * - 与麦克风共用BCLK和WS引脚
 * - 24kHz输出采样率
 */

#ifndef _MECHANICAL_FLOWER_AUDIO_CODEC_H_
#define _MECHANICAL_FLOWER_AUDIO_CODEC_H_

#include "audio_codec.h"              // 音频编解码器基类

#include <driver/gpio.h>              // GPIO驱动
#include <driver/i2s_std.h>           // I2S标准模式驱动
#include <mutex>                      // 互斥锁

/**
 * @class MechanicalFlowerAudioCodec
 * @brief 机械AI仿生花专用音频编解码器
 * 
 * 继承自AudioCodec基类，实现：
 * - INMP441麦克风的数据读取
 * - MAX98357A功放的数据输出
 */
class MechanicalFlowerAudioCodec : public AudioCodec {
private:
    std::mutex data_if_mutex_;        // 数据接口互斥锁，保护并发访问
    
    // INMP441 I2S麦克风引脚
    gpio_num_t mic_sck_;              // BCLK - 位时钟
    gpio_num_t mic_ws_;               // LRCLK/WS - 左右声道时钟
    gpio_num_t mic_din_;              // DIN - 数据输入
    
    // MAX98357A I2S功放引脚
    gpio_num_t amp_dout_;             // DOUT - 数据输出
    
    // I2S通道句柄
    i2s_chan_handle_t rx_handle_;     // I2S接收通道（麦克风）
    i2s_chan_handle_t tx_handle_;     // I2S发送通道（功放）

    /**
     * @brief 写入音频数据到扬声器
     * @param data 音频数据指针
     * @param samples 采样点数
     * @return 实际写入的采样点数
     */
    virtual int Write(const int16_t* data, int samples) override;

    /**
     * @brief 从麦克风读取音频数据
     * @param dest 目标缓冲区
     * @param samples 要读取的采样点数
     * @return 实际读取的采样点数
     * 
     * 实现细节：
     * 1. 从I2S读取32位数据
     * 2. 右移12位转换为16位有效数据
     * 3. 应用输入增益
     */
    virtual int Read(int16_t* dest, int samples) override;

    /**
     * @brief 启用/禁用麦克风输入
     * @param enable true=启用, false=禁用
     */
    virtual void EnableInput(bool enable) override;

    /**
     * @brief 启用/禁用扬声器输出
     * @param enable true=启用, false=禁用
     */
    virtual void EnableOutput(bool enable) override;

public:
    /**
     * @brief 构造函数
     * @param input_sample_rate 输入采样率（建议16000Hz）
     * @param output_sample_rate 输出采样率（建议24000Hz）
     * @param mic_sck I2S位时钟引脚 (BCLK) - 麦克风和功放共用
     * @param mic_ws I2S左右声道时钟引脚 (WS) - 麦克风和功放共用
     * @param mic_din I2S数据输入引脚 (DIN) - 连接INMP441
     * @param amp_dout I2S数据输出引脚 (DOUT) - 连接MAX98357A
     */
    MechanicalFlowerAudioCodec(int input_sample_rate, int output_sample_rate,
        gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din,
        gpio_num_t amp_dout = GPIO_NUM_NC);
    
    /**
     * @brief 析构函数
     * 释放I2S通道资源
     */
    virtual ~MechanicalFlowerAudioCodec();
};

#endif // _MECHANICAL_FLOWER_AUDIO_CODEC_H_