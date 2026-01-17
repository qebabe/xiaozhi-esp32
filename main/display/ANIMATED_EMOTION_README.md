# 动画表情功能说明

## 概述

动画表情功能为设备添加了生动、可动画的机器人眼睛显示，替代了原来的静态Font Awesome图标表情。通过LVGL原生实现，支持眨眼、位置移动和表情变化等多种动画效果。

## 功能特性

### 🎯 核心功能
- **眨眼动画**: 自动随机眨眼（2-5秒间隔），增加生动感
- **位置移动**: 眼睛可以看向8个不同方向
- **表情变化**: 支持多种表情（开心、悲伤、生气、惊讶等）
- **平滑过渡**: 所有动画都有平滑的过渡效果
- **双模式切换**: 支持"眼睛模式"和"默认模式"
- **语音控制**: 通过语音命令切换显示模式

### 🎨 支持的表情类型
- `neutral` - 中性表情 👁️
- `happy` - 开心表情 😊
- `sad` - 悲伤表情 😢
- `angry` - 生气表情 😠
- `surprised` - 惊讶表情 😲
- `thinking` - 思考表情 🤔
- `sleepy` - 困倦表情 😴
- `winking` - 眨眼表情 😉
- `confused` - 困惑表情 😕

### 📍 眼睛方向
- 0: 中央
- 1: 上方
- 2: 下方
- 3: 左侧

## 显示模式

### 🎭 眼睛模式 (Eye Mode)
- **特点**: 只显示动画眼睛，不显示任何文字
- **适用场景**: 想要专注的视觉体验时
- **显示内容**:
  - ✅ 动画眨眼眼睛
  - ❌ 状态文字（"STANDBY"等）
  - ❌ 时间显示
  - ❌ 聊天消息
  - ❌ 用户语音文字

### 📝 默认模式 (Default Mode)
- **特点**: 显示完整的文字和表情信息
- **适用场景**: 正常使用时
- **显示内容**:
  - ✅ 状态文字和时间
  - ✅ 聊天消息
  - ✅ 用户语音文字
  - ✅ 表情图标或动画（根据配置）

## 语音控制

### 🎤 支持的语音命令
可以通过语音说出以下关键词来切换显示模式：

**中文命令**:
- "切换模式"
- "切换显示"
- "眼睛模式"
- "默认模式"
- "文字模式"

**英文命令**:
- "change mode"
- "eye mode"
- "text mode"

### 🔄 切换逻辑
- 当前为默认模式 → 切换到眼睛模式
- 当前为眼睛模式 → 切换到默认模式

### 💡 使用提示
- 语音命令会被识别但不会显示在聊天界面
- 切换后会立即生效
- 支持中英文混合使用
- 4: 右侧
- 5: 左上方
- 6: 右上方
- 7: 左下方
- 8: 右下方

## 启用方法

### 方法1: 通过menuconfig启用（推荐）
在编译前通过menuconfig启用动画表情：

```bash
idf.py menuconfig
```

导航到 `Xiaozhi Assistant -> Enable Animated Emotion Display`，选择 `[*] Enable Animated Emotion Display`

然后重新编译：
```bash
idf.py build
idf.py flash
```

### 方法2: 运行时设置（临时）
通过代码运行时启用：

```cpp
auto display = Board::GetInstance().GetDisplay();
display->SetAnimatedEmotionMode(true);
```

### 2. 运行时切换
通过代码动态启用/禁用：

```cpp
auto display = Board::GetInstance().GetDisplay();
display->SetAnimatedEmotionMode(true);  // 启用
display->SetAnimatedEmotionMode(false); // 禁用
```

## 使用示例

### 基本表情设置
```cpp
// 设置开心表情
display->SetEmotion("happy");

// 设置眨眼表情
display->SetEmotion("winking");

// 设置思考表情
display->SetEmotion("thinking");
```

### 眼睛方向控制
```cpp
// 让眼睛看向右侧
display->SetEmotionDirection(4);

// 让眼睛看向左上方
display->SetEmotionDirection(5);
```

### 完整交互示例
```cpp
// 用户提问时 - 思考表情，看向用户
display->SetEmotion("thinking");
display->SetEmotionDirection(2); // 向下看

// 回答问题时 - 开心表情，看向前方
display->SetEmotion("happy");
display->SetEmotionDirection(0); // 中央

// 困惑时 - 困惑表情，左右移动
display->SetEmotion("confused");
display->SetEmotionDirection(3); // 左侧
// 然后可以切换到右侧
display->SetEmotionDirection(4); // 右侧
```

## 技术实现

### 架构设计
```
Display (基类)
├── Display (接口扩展)
│   ├── SetAnimatedEmotionMode()
│   ├── SetEmotionDirection()
│   └── UpdateAnimatedEmotion()
│
└── OledDisplay (具体实现)
    ├── AnimatedEmotion (动画引擎)
    │   ├── 眨眼动画
    │   ├── 位置动画
    │   └── 表情动画
    └── LVGL Canvas (渲染画布)
```

### 动画参数
- **眨眼间隔**: 3秒（可配置）
- **眨眼持续时间**: 150ms
- **最大帧率**: 30 FPS
- **过渡时间**: 位置800ms，表情500ms

### 性能优化
- 帧率控制，避免过度刷新
- 内存缓冲区复用
- 条件渲染，仅在需要时更新

## 文件结构

```
main/display/
├── animated_emotion.h/.cc     # 动画表情核心类
├── oled_display.h/.cc         # OLED显示器实现
├── display.h/.cc              # 显示器接口
└── ANIMATED_EMOTION_README.md # 本文档
```

## 兼容性

- ✅ **硬件**: 支持OLED显示器（128x32, 128x64等）
- ✅ **开发板**: 支持面包板、Qebabe Xiaoche等OLED开发板
- ✅ **软件**: ESP32平台，LVGL 8.x+
- ✅ **向后兼容**: 可随时切换回静态表情
- ⚠️ **默认状态**: 默认关闭，需要通过menuconfig启用模式

## 注意事项

1. **内存占用**: 动画模式会增加约4KB内存使用
2. **CPU占用**: 动画更新会轻微增加CPU使用率
3. **显示效果**: 在低亮度OLED上动画效果更佳

## 验证功能

### 检查启用状态
编译后，可以通过以下方式检查功能是否正常：

1. **查看编译日志**：
   ```
   ESP_LOGI("Application", "Animated emotion mode enabled via Kconfig");
   ```

2. **运行时检查**：
   ```cpp
   auto app = Application::GetInstance();
   ESP_LOGI("Test", "Display mode: %s",
            app.GetDisplayMode() == kDisplayModeDefault ? "Default" : "Eye Only");
   ```

### 观察效果

#### 默认模式下
- 显示状态文字和时间
- 显示聊天消息和用户语音文字
- 根据Kconfig配置决定是否显示动画表情

#### 眼睛模式下
- 只显示一对眨眼的机器人眼睛
- 没有状态文字、时间、聊天消息显示
- 眼睛会自动眨眼（2-5秒随机间隔）

### 测试语音控制
1. 对设备说："切换模式" 或 "眼睛模式"
2. 观察显示器是否切换到只显示眼睛
3. 再次说："切换模式" 或 "默认模式"
4. 观察是否恢复正常显示

## 故障排除

### 动画不显示
- 确认通过menuconfig启用了 `Enable Animated Emotion Display`
- 检查开发板类型是否在支持列表中
- 查看日志中的初始化错误信息
- 确认OLED显示器正常工作

### 动画卡顿
- 检查系统负载是否过高
- 确认OLED刷新率设置合理
- 考虑在系统忙碌时暂时禁用动画

### 表情切换延迟
- 动画过渡时间是正常现象（500-800ms）
- 确保定期调用 `UpdateAnimatedEmotion()`
- 检查主循环是否有阻塞操作

### 编译错误
- 确认所有源文件都已添加到CMakeLists.txt
- 检查头文件包含路径是否正确
- 确认LVGL版本兼容性

## 未来扩展

- 支持自定义表情形状
- 添加更多动画效果（如摇头、点头）
- 支持表情序列动画
- 添加声音同步的表情变化