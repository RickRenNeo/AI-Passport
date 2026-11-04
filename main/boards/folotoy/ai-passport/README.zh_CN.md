[English](/main/boards/folotoy/ai-passport/README.md) · **简体中文**

# FoloToy AI Passport (ai-passport)

让 XiaoZhi 语音助手运行在
[FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 可穿戴设备上的板级定义。

## 硬件

- MCU：ESP32-C3，8 MB Flash，**无 PSRAM**，USB Serial/JTAG 控制台
- 音频：ES8311 编解码器（I2C 0x18），挂在共享 I2S 总线上，I2S 全双工
  （功使能未接线，视为始终打开）
- 屏幕：ST7789（ST7789P3）240x320 竖屏，4-line SPI
- 电池：CW2017 电量计（I2C 0x63，可选）
- 按键：UP / DOWN / OK 通过电阻分压共享 GPIO0（ADC1_CH0）

引脚映射遵循 `ai-passport/components/bsp/include/bsp_pins.h`：

| 功能 | 引脚 |
| --- | --- |
| LCD MOSI / SCLK / CS / DC | 9 / 8 / 1 / 20 |
| LCD 背光（PWM） | 21 |
| I2S MCLK / BCLK / WS / DOUT / DIN | 6 / 5 / 3 / 2 / 4 |
| I2C SDA / SCL | 10 / 7 |
| 按键（ADC 分压） | GPIO0 |

## 构建

```sh
python scripts/build.py folotoy/ai-passport --name ai-passport
```

输出 `build/merged-binary.bin`（8 MB Flash，USB Serial/JTAG 控制台）。

## 控制

三个物理按键映射为 XiaoZhi 的语音助手动作：

- **OK**——单击：切换对话状态（启动时用于进入 Wi-Fi 配网模式）
- **UP**——单击：音量 +10；长按：最大音量
- **DOWN**——单击：音量 -10；长按：静音

由于分压共享一个 ADC 引脚，XiaoZhi 把它读成三个独立的 ADC 按键（与
ESP-BOX-Lite 相同的做法）。

## 电源管理

Passport 是电池供电的可穿戴设备，因此板级在三个阶段逐步放弃电源，全部从
最后一次按键开始计时：

| 空闲 | 阶段 | 发生什么 |
| --- | --- | --- |
| 60 s | 调暗 | 背光降到 10%，其他不变 |
| 360 s | 软休眠 | 面板 Sleep In、背光关闭、编解码关闭、CPU 降频到 40 MHz |
| 2160 s | 深度睡眠 | 从软休眠阶段进入；任意按键通过 GPIO0 唤醒并重启应用 |

调暗阶段跑在 `PowerSaveTimer` 上；后两个阶段跑在板级文件自己的 1 Hz `esp_timer`
上：它们的倒计时必须在请求被拒绝时继续走并重试，这是共享的 shutdown 请求
（每个空闲周期只有一个事件而不是心跳）无法表达的行为。定时器只负责计时和发起
切换，活动检查与所有面板、编解码、PM 动作都在主任务上，按键既停止倒计时也使
已经排队的状态切换失效。

`ai_passport_board.cc` 顶部的 `kDimSeconds`、`kSoftSleepSeconds`、
`kDeepSleepSeconds`、`kDimBrightness` 与 `kStandbyCpuMinFreq` 用于调整该策略。
刻意保留的三个性质：

- **软休眠阶段是刻意做浅的。** 它约 20 mA，半小时约 10 mAh，占 520 mAh 电量的
  2% 左右。CPU 仍在运行，一次按键约 1 秒即可恢复整机，对话、页面与会话都保留，
  而不是重启进全新的 idle。
- **深度睡眠兜底正是让上面这一点可负担的原因。** 它从软休眠阶段而不是直接进入，
  所以被遗忘的 Passport 仍在 36 分钟内落到深度睡眠电流。无边界的"只软休眠"
  策略前半小时更好看，然后会让电池在一夜之间耗尽。
- **刻意不打开自动 light sleep**，尽管它能让软休眠窗口低于 1 mA：三个截止时间
  背后的倒计时跑在以 `skip_unhandled_events = true` 创建的 `esp_timer` 上，
  这种定时器无法把芯片从 light sleep 唤醒，因此打开 light sleep 会把
  60 / 360 / 2160 悄悄拉长成没人要求的墙上时间。`SetStandbyClock()` 只改 CPU
  频率，没有这个问题；`config.json` 里的 `CONFIG_PM_ENABLE` 才让这个调用生效。

每个阶段都由 `Application::CanEnterSleepMode()` 把关，进行中的对话或播放会推迟
截止时间而不是被打断。任意按键（`OnPressDown`，长按也算）都会取消倒计时，并从
软休眠阶段唤回设备。配网页面写入的 NVS `sleep_mode` 标志可以一次禁用三个阶段。

这个把关有两个值得知道的后果：

- 永远到不了 `kDeviceStateIdle` 的板级会一直亮屏。常见情形是未配网、停在 Wi-Fi
  配网模式的板级，状态机只允许它进入激活或音频测试。在 idle 之外休眠是影响所有
  板级的框架级改动，这里不做——未配置的 Passport 应该被配置，而不是被扔在架子上。
- 板级在两次阶段切换中都会重新检查该条件，并每秒重试软休眠阶段直到被接受。
  唤醒词发起的对话不按键，因此永远不会重置倒计时；没有重新检查与重试，它要么
  被深度睡眠兜底切断，要么直到下一次按键才重新获得软休眠阶段。

### 关屏与关音频

关闭音频链路是最容易做错的部分。音频输入任务每次开始读取都会重新使能编解码器，
所以从板级调用 `EnableInput(false)` 会立刻被抵消。该阶段改为关闭唤醒词
（`AudioService::EnableWakeWordDetection(false)`），把编解码器留给 `AudioService`，
后者在输入空闲 `AUDIO_POWER_TIMEOUT_MS`（15 s）后自行关闭 PCM 链路。唤醒路径
必须显式把唤醒词切回：应用状态机的 idle 分支不会自己再跑一次，否则设备在本次
启动余下的时间里再也听不到唤醒词。

无线电保持开启。`WifiManager::StopStation()` 约值 10 mA，但在硬件上实测得不偿失：
停止基站后 MQTT 协议会不断重试——`esp_mqtt` 自己的定时器加上协议 60 秒的定时器
——应用一旦回到 idle，其中一次尝试就会把 `Lang::Strings::SERVER_NOT_CONNECTED`
以用户可见告警的形式抛出来，还会播放通知音并在本该休眠的设备上短暂重开编解码器。
该告警会锁定，下一次按键后仍留在屏幕上。保持链路可以避免这一切，保留服务器推送
与存活会话，并且因为不需要重连 Wi-Fi 而即时唤醒。窗口越长算术越不同：八小时量级
下无线电约占电量的三分之一，因此更长的窗口必须停止基站并让协议在其下线期间安静。

三个按键在每个阶段都能唤醒设备，但唤醒的那一次按下不能同时触发动作：

- 一个唤醒按键守卫整体丢弃一次按下，而不是一个时间窗。它在把设备从软休眠唤醒的
  按下沿上被武装，或从深睡唤醒后按键组件启动时采到的电平上武装，在组件报告该次
  按下序列结束（`BUTTON_PRESS_END`）时撤防。这正是长按被拦下的原因：组件在按下
  开始 2 秒后才判定长按，硬件实测这个值已经超过任何短到不会误伤正常点击的固定窗口。
  `kWakeKeyGuardMs`（10 s）只是收不到 release 事件时的兜底。
- 深睡唤醒时按键已经松开的场景不武装守卫，下一次按正常工作——没有唤醒按下需要丢弃。
- 冷启动没有守卫，因此 `kDeviceStateStarting` 期间的 OK 仍是配网入口。
- 网络不通时按 OK 显示普通的"连接中"提示，而不是打不开音频通道并抛告警。这覆盖了
  息屏期间链路自行断开的情况。配网与 Wi-Fi 配置页的喇叭测试不开音频通道，因此不受
  基站状态限制。

### 深度睡眠

深度睡眠遵循 FoloToy AI Passport BSP 的关机契约
（BSP 仓库中的 `docs/reference/shinku-chen/deep-sleep-peripheral-power-off`），顺序如下：

1. 清空所有已武装的唤醒源
   （`esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)`），然后用 IDF 的
   `esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown()` 武装 GPIO0 低电平唤醒。
   自动 light sleep 会为下一个定时事件保留 RTC 定时器，继承它的深睡会在下一个
   OS tick 再次唤醒——硬件实测为入睡约 1 秒后复位，报 `ESP_SLEEP_WAKEUP_TIMER`
   且 GPIO 唤醒状态为空。ESP32-C3 没有 EXT0/EXT1，且该 GPIO 调用接收的是引脚
   *位掩码*，因此传 `GPIO_NUM_0` 而不是 `1ULL << GPIO_NUM_0` 会静默地什么都不武装。
   武装失败时板级退化为定时唤醒，避免设备永远睡死。
2. CW2017 `CONFIG=0xF0`，5 毫秒后回读，不一致再试一次。板级初始化用电荷的
   "重启—激活"周期反转该操作，因此深睡前睡过的电量计在唤醒后仍能上报。
3. ES8311 挂起：先用 `esp_codec_dev` disable/close，再通过板级自己的 I2C 句柄写入
   BSP 的挂起寄存器序列，并回读关键寄存器。REG0E 按厂商值写 `0xFF`，但只有
   6:0 位有意义——该器件第 7 位不锁存，回读为 `0x7F`，这正是检查所期望的。
   BSP 通过掩码检查同样六个寄存器（`bsp_es8311_sleep_check.c`）。
   随后显式停止两个 I2S 通道，避免引脚释放、主时钟静默后编解码器仍在驱动输入。
   编解码器构造只会使能一次通道，只有 `esp_codec_dev_close()` 能再次停止它们，
   因此自本次启动以来从未播放过的板级到达此处时时钟仍在跑。
4. MCLK/BCLK/WS/DOUT/DIN 释放为无上下拉的输入。
5. SDA/SCL 同样释放（此后任何 I2C 事务都不再有效）。
6. 在 LVGL 锁下：面板 display-off 与 Sleep In，背光 PWM 停到 0，随后
   CS 拉高 / SCLK / MOSI / DC / 背光拉低，并用 `gpio_deep_sleep_hold_en()` 保持到深睡。

失败的步骤只记录、绝不中止关机，设备不会带着半关的板级留在唤醒态。深睡若意外
返回，板级直接重启而不是尝试恢复已经释放的总线。

引脚保持锁存在 RTC 域，跨过深睡复位，因此回来时必须对每个被保持的引脚调用
`gpio_hold_dis()`。`ReleaseDeepSleepHolds()` 在板级构造函数里第一件事做这个。
在本板上，唤醒后仍然保持的是 GPIO1（LCD CS，位于 `RTC_CNTL_PAD_HOLD_REG`）与
GPIO8/GPIO9（LCD SCLK/MOSI，位于 `RTC_CNTL_DIG_PAD_HOLD_REG`）；不释放的话
它们一直被拉住，面板永远回不来。

软件修不了的部分：Passport 没有把功放使能接到 MCU（`AUDIO_CODEC_PA_PIN` 为
`NC`），因此功放待机电流、稳压器静态电流、外部 I2C 上拉与电芯自放电都还在。
如果待机电流仍然偏高，请在硬件上隔离这些部分。

硬件验证状态（ESP32-C3，IDF 6.1）。已在设备上验证：板级初始化与稳定 idle 循环；
配网后进入 `kDeviceStateIdle`；调暗阶段按时触发、软休眠阶段在其后 300 s、深度睡眠
兜底在 36 分钟截止点，且都在预期墙上时间发生（uptime 65 s 时打印 `Idle 60s`、365 s 时
打印 `Idle 360s`，扣掉 4.7 s 启动；兜底在 boot + 2165 s，也正是按键把设备从深睡唤醒的
时刻）；CPU 降频只产生一次 `esp_pm_configure` 调用，由内核的
`pm: Frequency switching config: ... Light sleep: DISABLED` 确认；505 秒的软休眠窗口
全程安静——无 MQTT 错误、无告警、无协议或基站断开、无编解码重开；软休眠唤醒恢复
面板、背光与时钟，且 Wi-Fi 零活动（无基站启动、停止、扫描或 DHCP——链路从未断开），
编解码器按需重开，之后一次完整语音交互（唤醒 -> 对话 -> 识别 -> 回答 -> 音量键）照常
工作；以及从软休眠唤醒的那一次单击 OK 被整体吞掉——其后没有状态切换、没有开麦、
没有 MQTT 流量；以及深睡唤醒时按键仍按住的情况下，该次按下没有被判成长按——没有静音。
两次运行使用的是早期的"窗口 + release"守卫；此后守卫已重构为整体丢弃一次按下序列，
重构后的路径（软休眠唤醒为短按、为长按，深睡唤醒时按键已松开，以及
`kDeviceStateStarting` 期间的 OK）仍需在硬件上重新检查。对于更早直接进深睡的版本：
验证了 CW2017 睡眠及回读一致；ES8311 挂起序列通过寄存器回读；I2S/I2C 引脚释放；
深睡真正粘住（残留唤醒源不会自醒）；按键唤醒设备时
`esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO` 且
`esp_sleep_get_gpio_wakeup_status() == 0x1`；以及深睡引脚保持回来时被释放（通过
`RTC_CNTL_PAD/DIG_PAD_HOLD` 寄存器回读确认）。关机序列本身也通过当前代码路径验证：
进入兜底时 `CW2017 asleep (CONFIG=0xF0 verified)`、
`ES8311 suspended and verified (attempt 1)` 与 I2S/I2C 引脚释放均报告干净。

未验证：idle、软休眠与深度睡眠的电流（上面 20 mA 是按 BOM 估算，不是测量）；
以及在外部 10 kOhm 上拉之外，关闭 `CONFIG_ESP_SLEEP_GPIO_ENABLE_INTERNAL_RESISTORS`
是否还能省电。阶段定时的倒计时此后已从 `PowerSaveTimer` 的 shutdown 请求移到板级
自己的定时器：预期仍是同样的 60 / 360 / 2160 s 截止时间，但该改动后未在硬件上重新测量。
构建通过不等于硬件验证。

关于日志里的 `i2s_common: i2s_channel_disable ... has not been enabled yet`：
它来自 `esp_codec_dev` 自身在 `audio_testing` 状态重开 PCM 链路时的挂起 disable
记账，不是本板 I2S 处理的问题——板级会报告它实际改动了多少个通道，这条路径里是 0。

软休眠阶段保持 CPU 唤醒，因此 `pm: Frequency switching config:` 显示
`Light sleep: DISABLED` 是设计使然；后续若要打开它，必须先把三个截止时间从
`skip_unhandled_events` 定时器上移走。

## 备注 / 校准

- 屏幕方向、颜色反转与背光 PWM 极性取自 Passport BSP；请在真机上验证，若画面旋转/
  反转或背光相反，调整 `config.h` 中的 `DISPLAY_*` 宏。
- ADC 按键电压窗口假设 Passport 外部 10 kOhm 上拉。若门限漂移，用 Button 页面重新测量。
- CW2017 是可选的；没有该芯片时状态栏不显示电量，板级照常工作。
- `CONFIG_USE_ESP_WAKE_WORD` 是关闭的，因此本板是按键驱动。打开它约消耗 60 KB 堆
  （硬件实测：最低可用堆从 47-70 KB 降到 9.8 KB），并且与软休眠阶段矛盾——后者之所以
  释放麦克风，正是因为不需要唤醒词持续监听。
- `ES8311_CODEC_DEFAULT_ADDR` 是 8 位形式（`0x30`）；`i2c_device_config_t` 接收
  7 位地址。esp_codec_dev 在构建自己的句柄时会移位，此处的终端挂起序列走板级自己的
  句柄，因此也移位（`kEs8311I2cAddress`）。用不移位的地址等于和空气对话：硬件实测
  每次寄存器回读都返回 `0xF0`，两次挂起尝试都失败。
