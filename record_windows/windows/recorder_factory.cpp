#include "recorder_interface.h"
#include "windows_version.h"
#include "mf_recorder.h"
#include "fmedia_recorder.h"
#include <memory>

namespace record_windows
{
    std::unique_ptr<IRecorder> RecorderFactory::CreateRecorder(
        EventStreamHandler<>* stateEventHandler,
        EventStreamHandler<>* recordEventHandler)
    {
        // 根据Windows版本选择不同的录音器实现
        if (IsWindows10Plus())
        {
            // Windows 10及以上版本使用MediaFoundation
            return std::make_unique<MediaFoundationRecorder>(stateEventHandler, recordEventHandler);
        }
        else
        {
            // Windows 7和8使用fmedia
            return std::make_unique<FmediaRecorder>(stateEventHandler, recordEventHandler);
        }
    }
} 