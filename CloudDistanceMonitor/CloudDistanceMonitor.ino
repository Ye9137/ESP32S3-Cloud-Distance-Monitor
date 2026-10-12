/**
 * 功能介绍：HC-SR04 测距、中文 OLED 显示，并通过 ThingsCloud MQTT 上报真实距离。
 */

#include <ThingsCloudWiFiManager.h>
#include <ThingsCloudMQTT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>

// 本地连接信息：复制 secrets.example.h 为 secrets.h 后填写。
#include "secrets.h"

ThingsCloudMQTT client(
    THINGSCLOUD_MQTT_HOST,
    THINGSCLOUD_DEVICE_ACCESS_TOKEN,
    THINGSCLOUD_PROJECT_KEY);

// 以当前实物接线为准：OLED SDA=GPIO36、SCL=GPIO35。
// N16R8 的 GPIO35/36 与内部 PSRAM 共用；若开机自检仍黑屏，需将 OLED 改接原理图的 GPIO1/2。
const int LED_PIN = 5;
const int OLED_SDA_PIN = 36;
const int OLED_SCL_PIN = 35;
const int TRIG_PIN = 18;
const int ECHO_PIN = 8;
const uint32_t OLED_I2C_CLOCK_HZ = 100000;

const unsigned long measure_interval = 100;
const unsigned long display_interval = 200;
const unsigned long display_retry_interval = 5000;
const unsigned long echo_timeout_us = 30000;
const float DISTANCE_ALARM_CM = 30.0f; // 实测距离严格小于此值时报警
const unsigned long alarm_blink_interval = 250;

// 固定屏幕通信频率；驱动默认在发送画面时会切到 400 kHz。
Adafruit_SSD1306 oled(128, 64, &Wire, -1,
                     OLED_I2C_CLOCK_HZ, OLED_I2C_CLOCK_HZ);
U8G2_FOR_ADAFRUIT_GFX screenText;

enum DistanceState { DISTANCE_WAITING, DISTANCE_VALID, DISTANCE_NO_ECHO, DISTANCE_OUT_OF_RANGE };
DistanceState distanceState = DISTANCE_WAITING;
float distanceCm = 0.0f;
bool distanceAlarm = false;
bool alarmLedOn = false;
bool oledReady = false;
uint8_t oledAddress = 0;
unsigned long lastMeasureMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastDisplayRetryMs = 0;
unsigned long lastAlarmBlinkMs = 0;

void setDistanceAlarm(bool active)
{
    if (distanceAlarm == active) return;
    distanceAlarm = active;
    alarmLedOn = active;
    digitalWrite(LED_PIN, active ? HIGH : LOW);
    lastAlarmBlinkMs = millis();
    Serial.printf("Distance alarm %s\n", active ? "ON" : "OFF");
}

void updateAlarmLed(unsigned long now)
{
    if (!distanceAlarm) return;
    if (now - lastAlarmBlinkMs < alarm_blink_interval) return;
    lastAlarmBlinkMs = now;
    alarmLedOn = !alarmLedOn;
    digitalWrite(LED_PIN, alarmLedOn ? HIGH : LOW);
}

bool oledResponds(uint8_t address)
{
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
}

void connectDisplay()
{
    const uint8_t addresses[] = {0x3C, 0x3D};
    oledReady = false;
    for (uint8_t address : addresses)
    {
        if (!oledResponds(address)) continue;
        if (!oled.begin(SSD1306_SWITCHCAPVCC, address, false, false))
        {
            Serial.println("OLED framebuffer allocation failed");
            return;
        }
        Wire.setClock(OLED_I2C_CLOCK_HZ);
        Wire.setTimeOut(50);
        oled.ssd1306_command(SSD1306_SETCONTRAST);
        oled.ssd1306_command(160);
        if (!oledResponds(address)) continue;
        oledAddress = address;
        oledReady = true;
        Serial.printf("OLED connected at 0x%02X\n", address);
        return;
    }
    Serial.printf("OLED not found: SDA=GPIO%d, SCL=GPIO%d\n",
                  OLED_SDA_PIN, OLED_SCL_PIN);
}

void measureDistance()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    const unsigned long echoUs = pulseIn(ECHO_PIN, HIGH, echo_timeout_us);
    if (echoUs == 0)
    {
        distanceState = DISTANCE_NO_ECHO;
        setDistanceAlarm(false);
        return;
    }

    // 声速约 0.0343 cm/us；回波往返一次，距离取一半。
    const float measuredCm = echoUs * 0.0343f * 0.5f;
    if (measuredCm < 2.0f || measuredCm > 400.0f)
    {
        distanceState = DISTANCE_OUT_OF_RANGE;
        setDistanceAlarm(false);
        return;
    }
    distanceCm = measuredCm;
    distanceState = DISTANCE_VALID;
    setDistanceAlarm(distanceCm < DISTANCE_ALARM_CM);
}

void drawDisplay()
{
    if (!oledReady) return;
    if (!oledResponds(oledAddress))
    {
        oledReady = false;
        return;
    }
    oled.clearDisplay();
    screenText.setFont(u8g2_font_wqy12_t_gb2312);
    screenText.drawUTF8(28, 13, "超声波测距");
    oled.drawFastHLine(0, 17, 128, SSD1306_WHITE);
    screenText.drawUTF8(0, 39, "距离");

    if (distanceState == DISTANCE_VALID)
    {
        char value[20];
        snprintf(value, sizeof(value), "%.1f cm", distanceCm);
        screenText.setFont(u8g2_font_7x14_tf);
        screenText.drawUTF8(38, 40, value);
    }
    else
    {
        screenText.setFont(u8g2_font_7x14_tf);
        screenText.drawUTF8(38, 40, "--.- cm");
    }

    screenText.setFont(u8g2_font_wqy12_t_gb2312);
    screenText.drawUTF8(0, 61, "下限");
    char limitText[16];
    snprintf(limitText, sizeof(limitText), "%.0f cm", DISTANCE_ALARM_CM);
    screenText.setFont(u8g2_font_7x14_tf);
    screenText.drawUTF8(28, 61, limitText);

    screenText.setFont(u8g2_font_wqy12_t_gb2312);
    if (distanceAlarm)
    {
        screenText.drawUTF8(85, 61, "报警");
    }
    else
    {
        const char *status = distanceState == DISTANCE_VALID ? "正常" :
                             distanceState == DISTANCE_WAITING ? "待测" :
                             distanceState == DISTANCE_NO_ECHO ? "无回波" : "超程";
        screenText.drawUTF8(85, 61, status);
    }
    oled.display();
}
// 上报数据的间隔时间计时器
unsigned long timer1 = millis();
// 设置定时上报数据的时间间隔，单位是 ms。免费版项目请务必大于30秒，否则设备可能会被限连。
const int report_interval = 1000 * 60 * 5;

void setup()
{
    Serial.begin(115200);

    // LED 在原理图的 GPIO5，距离超过上限时闪烁。
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(TRIG_PIN, OUTPUT);
    digitalWrite(TRIG_PIN, LOW);
    pinMode(ECHO_PIN, INPUT);

    Serial.printf("OLED wiring: SDA=GPIO%d SCL=GPIO%d\n",
                  OLED_SDA_PIN, OLED_SCL_PIN);
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN, OLED_I2C_CLOCK_HZ);
    Wire.setTimeOut(50);
    delay(150); // 等屏幕电源稳定后再探测地址。
    screenText.begin(oled);
    screenText.setFontMode(1);
    screenText.setForegroundColor(SSD1306_WHITE);
    screenText.setBackgroundColor(SSD1306_BLACK);
    connectDisplay();
    if (oledReady)
    {
        // 开机全屏亮 300 ms：可直接区分供电/通信问题与文字绘制问题。
        oled.fillScreen(SSD1306_WHITE);
        oled.display();
        delay(300);
    }
    drawDisplay();
    lastMeasureMs = lastDisplayMs = lastDisplayRetryMs = millis();

    // 允许 SDK 的日志输出
    client.enableDebuggingMessages();

    // 连接 WiFi AP
    client.setWifiCredentials(ssid, password);
    timer1 = millis();
}

// 必须实现这个回调函数，当 MQTT 连接成功后执行该函数。
void onMQTTConnect()
{
    // GPIO5 已用于距离报警；联网成功仍通过串口确认。
    Serial.println("ThingsCloud MQTT connected");
}

void pubSensors()
{
    // 保留原有 MQTT 上报流程；仅上报本次有效的真实距离，不上传模拟值。
    if (distanceState != DISTANCE_VALID) return;
    DynamicJsonDocument obj(512);
    obj["distance"] = distanceCm;
    char attributes[512];
    serializeJson(obj, attributes);
    // 调用属性上报方法
    client.reportAttributes(attributes);
}

void loop()
{
    client.loop();

    unsigned long now = millis();
    if (now - lastMeasureMs >= measure_interval)
    {
        lastMeasureMs = now;
        measureDistance();
    }

    now = millis();
    updateAlarmLed(now);
    if (now - lastDisplayMs >= display_interval)
    {
        lastDisplayMs = now;
        drawDisplay();
    }

    if (now - lastDisplayRetryMs >= display_retry_interval)
    {
        lastDisplayRetryMs = now;
        if (oledReady && !oledResponds(oledAddress)) oledReady = false;
        if (!oledReady)
        {
            connectDisplay();
            drawDisplay();
        }
    }

    // 按间隔时间上报传感器数据
    if (now - timer1 > report_interval)
    {
        timer1 = now;
        pubSensors();
    }
}
