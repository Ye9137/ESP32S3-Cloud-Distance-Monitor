# ESP32-S3 云端超声波测距

使用 ESP32-S3、HC-SR04 和 I2C OLED 实现中文测距显示、近距离 LED 报警，通过 ThingsCloud MQTT 上报距离。

## 功能

- 每 100 ms 测距，每 200 ms 更新屏幕，接受 2–400 cm 的有效读数。
- 有效距离严格小于 30 cm 时报警，GPIO5 的 LED 每 250 ms 翻转；无效数据不报警。
- 每五分钟尝试上报有效距离，ThingsCloud 数值属性名为 `distance`，单位 cm。
- OLED 自动探测 `0x3C` / `0x3D`，掉线后每 5 秒重试。

## 默认接线

| 模块 | 信号 | GPIO |
|---|---|---|
| OLED | SDA / SCL | 36 / 35 |
| HC-SR04 | TRIG / ECHO | 18 / 8 |
| LED | 输出 | 5 |

保留原程序的引脚设置。原工程说明指出 ESP32-S3-N16R8 的 GPIO35/36 与内部 PSRAM 冲突；使用该模组时应核对板卡资料，原工程给出的替代方案是 SDA=GPIO1、SCL=GPIO2，并同步修改程序常量。

HC-SR04 的供电需符合所用模块规格；若采用 5V 供电，ECHO 接入 ESP32 前需降至 3.3V。LED 应串联限流电阻，所有模块共地。

## 使用方法

1. 在 Arduino IDE 安装 ESP32 开发板支持，选择实际 ESP32-S3 板卡。
2. 安装依赖：`ThingsCloud_ESP_SDK`、`Adafruit SSD1306`、`Adafruit GFX Library`、`Adafruit BusIO`、`U8g2` 和 `U8g2_for_Adafruit_GFX`。
3. 打开 `CloudDistanceMonitor/CloudDistanceMonitor.ino`。
4. 把同目录 `secrets.example.h` 复制为 `secrets.h`，在本地填写 Wi-Fi 名称、密码、MQTT Host、AccessToken 和 ProjectKey。模板全部留空；`secrets.h` 已加入 Git 忽略规则。
5. 在 ThingsCloud 设置数值属性 `distance`；通过源码顶部常量调整报警下限和引脚。
6. 烧录时按实际板卡选择端口和配置；串口日志波特率为 115200。

不要把填写后的 `secrets.h` 或包含连接信息的日志提交到仓库。模板未填写时不具备可用的云端连接配置。

## 许可证

维护者拥有权利的原创代码和文档采用 [MIT License](LICENSE)。第三方库、启动代码、示例衍生部分和字模不因仓库根目录的 MIT 文件而更改原有许可，见 [第三方说明](THIRD_PARTY_NOTICES.md)。

## 整理状态

本次发布整理仅进行了文件、隐私和源码引用核对，未编译、烧录或进行硬件测试。
