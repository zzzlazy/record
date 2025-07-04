#pragma once

#include <memory>
#include <string>
#include <map>
#include <windows.h>
#include "record_config.h"
#include "event_stream_handler.h"

using namespace flutter;

namespace record_windows
{
    enum RecordState {
        pause, record, stop
    };

    // 录音器抽象接口
    class IRecorder
    {
    public:
        virtual ~IRecorder() = default;

        virtual HRESULT Start(std::unique_ptr<RecordConfig> config, std::wstring path) = 0;
        virtual HRESULT StartStream(std::unique_ptr<RecordConfig> config) = 0;
        virtual HRESULT Pause() = 0;
        virtual HRESULT Resume() = 0;
        virtual HRESULT Stop() = 0;
        virtual HRESULT Cancel() = 0;
        virtual bool IsPaused() = 0;
        virtual bool IsRecording() = 0;
        virtual HRESULT Dispose() = 0;
        virtual std::map<std::string, double> GetAmplitude() = 0;
        virtual std::wstring GetRecordingPath() = 0;
        virtual HRESULT isEncoderSupported(const std::string encoderName, bool* supported) = 0;
    };

    // 录音器工厂类
    class RecorderFactory
    {
    public:
        static std::unique_ptr<IRecorder> CreateRecorder(
            EventStreamHandler<>* stateEventHandler,
            EventStreamHandler<>* recordEventHandler
        );
    };
} 