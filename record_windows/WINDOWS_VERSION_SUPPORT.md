# Windows版本支持说明

## 概述

本项目现在支持双后端系统，可以根据Windows版本自动选择最适合的录音后端：

- **Windows 10及以上版本**：使用MediaFoundation API
- **Windows 7和8**：使用fmedia.exe外部程序

## 技术详情

### 自动检测机制

系统会在运行时自动检测Windows版本：

```cpp
// 检测Windows版本
WindowsVersion GetWindowsVersion();
bool IsWindows7();
bool IsWindows10Plus();
```

### 录音器工厂

`RecorderFactory`类负责根据Windows版本创建合适的录音器实例：

```cpp
std::unique_ptr<IRecorder> RecorderFactory::CreateRecorder(
    EventStreamHandler<>* stateEventHandler,
    EventStreamHandler<>* recordEventHandler)
{
    if (IsWindows10Plus())
    {
        // Windows 10+ 使用MediaFoundation
        return std::make_unique<MediaFoundationRecorder>(stateEventHandler, recordEventHandler);
    }
    else
    {
        // Windows 7/8 使用fmedia
        return std::make_unique<FmediaRecorder>(stateEventHandler, recordEventHandler);
    }
}
```

## 部署说明

### Windows 10及以上版本

- 只需要编译好的DLL文件
- 依赖系统内置的MediaFoundation API
- 无需额外的可执行文件

### Windows 7和8

- 需要包含`fmedia`文件夹及其内容
- 必须包含以下文件：
  - `fmedia/fmedia.exe` - 主程序
  - `fmedia/mod/` - 模块目录
    - `afilter.dll`
    - `core.dll`
    - `flac.dll`
    - `fmt.dll`
    - `mpeg.dll`
    - `net.dll`
    - `opus.dll`
    - `vorbis.dll`

## 功能对比

| 功能           | MediaFoundation | fmedia |
|----------------|------------------|--------|
| 文件录音       | ✅               | ✅     |
| 流录音         | ✅               | ❌     |
| 暂停/恢复      | ✅               | ✅     |
| 实时振幅       | ✅               | ❌     |
| 设备选择       | ✅               | ✅     |
| AAC编码        | ✅               | ✅     |
| FLAC编码       | ✅               | ✅     |
| WAV编码        | ✅               | ✅     |
| Opus编码       | ✅               | ✅     |

## 故障排除

### Windows 7崩溃问题

如果在Windows 7上遇到MFPlat.dll相关的崩溃，这表明：

1. 系统正在尝试使用MediaFoundation后端
2. 版本检测可能有问题
3. 可以手动调试版本检测逻辑

### fmedia文件缺失

如果在Windows 7/8上遇到录音失败，检查：

1. `fmedia/fmedia.exe`是否存在
2. `fmedia/mod/`目录是否包含所有必需的DLL
3. 文件是否有执行权限

## 开发说明

### 添加新的录音器后端

1. 创建实现`IRecorder`接口的新类
2. 在`RecorderFactory::CreateRecorder`中添加选择逻辑
3. 更新CMakeLists.txt添加新的源文件

### 测试

建议在以下环境中测试：

- Windows 7 SP1
- Windows 8.1
- Windows 10
- Windows 11

确保每个版本都能正确选择合适的后端。 