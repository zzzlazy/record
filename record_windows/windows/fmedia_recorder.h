#pragma once

#include "recorder_interface.h"
#include <process.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

namespace record_windows
{
    // 基于fmedia.exe的录音器实现（用于Windows 7）
    class FmediaRecorder : public IRecorder
    {
    public:
        FmediaRecorder(EventStreamHandler<>* stateEventHandler, EventStreamHandler<>* recordEventHandler);
        virtual ~FmediaRecorder();

        HRESULT Start(std::unique_ptr<RecordConfig> config, std::wstring path) override;
        HRESULT StartStream(std::unique_ptr<RecordConfig> config) override;
        HRESULT Pause() override;
        HRESULT Resume() override;
        HRESULT Stop() override;
        HRESULT Cancel() override;
        bool IsPaused() override;
        bool IsRecording() override;
        HRESULT Dispose() override;
        std::map<std::string, double> GetAmplitude() override;
        std::wstring GetRecordingPath() override;
        HRESULT isEncoderSupported(const std::string encoderName, bool* supported) override;

    private:
        void UpdateState(RecordState state);
        std::wstring GetFmediaPath();
        std::wstring GetFileNameSuffix(const std::string& encoderName);
        std::vector<std::wstring> GetEncoderSettings(const std::string& encoderName, int bitRate);
        std::vector<std::wstring> GetAacQuality(int bitRate);
        HRESULT CallFmedia(const std::vector<std::wstring>& arguments);
        HRESULT EndRecording();

        EventStreamHandler<>* m_stateEventHandler;
        EventStreamHandler<>* m_recordEventHandler;
        RecordState m_recordState;
        std::wstring m_recordingPath;
        std::unique_ptr<RecordConfig> m_pConfig;
        PROCESS_INFORMATION m_processInfo;
        std::atomic<bool> m_processRunning;
        std::mutex m_stateMutex;
        double m_amplitude;
        double m_maxAmplitude;
        
        static const std::wstring FMEDIA_PATH;
        static const std::wstring FMEDIA_BIN;
        static const std::wstring PIPE_PROC_NAME;
    };
} 