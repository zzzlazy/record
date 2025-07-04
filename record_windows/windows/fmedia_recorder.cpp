#include "fmedia_recorder.h"
#include "record_windows_plugin.h"
#include <shlwapi.h>
#include <random>
#include <algorithm>

namespace record_windows
{
    const std::wstring FmediaRecorder::FMEDIA_PATH = L"fmedia";
    const std::wstring FmediaRecorder::FMEDIA_BIN = L"fmedia.exe";
    const std::wstring FmediaRecorder::PIPE_PROC_NAME = L"record_windows";

    FmediaRecorder::FmediaRecorder(EventStreamHandler<>* stateEventHandler, EventStreamHandler<>* recordEventHandler)
        : m_stateEventHandler(stateEventHandler),
          m_recordEventHandler(recordEventHandler),
          m_recordState(RecordState::stop),
          m_processRunning(false),
          m_amplitude(-160.0),
          m_maxAmplitude(-160.0)
    {
        ZeroMemory(&m_processInfo, sizeof(m_processInfo));
    }

    FmediaRecorder::~FmediaRecorder()
    {
        Dispose();
    }

    HRESULT FmediaRecorder::Start(std::unique_ptr<RecordConfig> config, std::wstring path)
    {
        HRESULT hr = Stop();
        if (FAILED(hr)) return hr;

        m_pConfig = std::move(config);
        m_recordingPath = path;

        // 如果文件已存在，删除它
        if (PathFileExists(path.c_str()))
        {
            DeleteFile(path.c_str());
        }

        // 构建fmedia参数
        std::vector<std::wstring> args = {
            L"--notui",
            L"--background",
            L"--record",
            L"--out=" + path,
            L"--rate=" + std::to_wstring(m_pConfig->sampleRate),
            L"--channels=" + std::to_wstring(m_pConfig->numChannels),
            L"--globcmd=listen",
            L"--gain=6.0"
        };

        // 添加设备设置
        if (!m_pConfig->deviceId.empty())
        {
            std::wstring deviceId(m_pConfig->deviceId.begin(), m_pConfig->deviceId.end());
            args.push_back(L"--dev-capture=" + deviceId);
        }

        // 添加编码器设置
        auto encoderSettings = GetEncoderSettings(m_pConfig->encoderName, m_pConfig->bitRate);
        args.insert(args.end(), encoderSettings.begin(), encoderSettings.end());

        hr = CallFmedia(args);
        if (SUCCEEDED(hr))
        {
            UpdateState(RecordState::record);
        }

        return hr;
    }

    HRESULT FmediaRecorder::StartStream(std::unique_ptr<RecordConfig> config)
    {
        // fmedia不支持流模式，返回不支持错误
        return E_NOTIMPL;
    }

    HRESULT FmediaRecorder::Pause()
    {
        if (m_recordState == RecordState::record)
        {
            HRESULT hr = CallFmedia({ L"--globcmd=pause" });
            if (SUCCEEDED(hr))
            {
                UpdateState(RecordState::pause);
            }
            return hr;
        }
        return S_OK;
    }

    HRESULT FmediaRecorder::Resume()
    {
        if (m_recordState == RecordState::pause)
        {
            HRESULT hr = CallFmedia({ L"--globcmd=unpause" });
            if (SUCCEEDED(hr))
            {
                UpdateState(RecordState::record);
            }
            return hr;
        }
        return S_OK;
    }

    HRESULT FmediaRecorder::Stop()
    {
        return EndRecording();
    }

    HRESULT FmediaRecorder::Cancel()
    {
        auto recordingPath = GetRecordingPath();
        HRESULT hr = EndRecording();

        if (SUCCEEDED(hr) && !recordingPath.empty())
        {
            DeleteFile(recordingPath.c_str());
        }

        return hr;
    }

    bool FmediaRecorder::IsPaused()
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_recordState == RecordState::pause;
    }

    bool FmediaRecorder::IsRecording()
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_recordState == RecordState::record;
    }

    HRESULT FmediaRecorder::Dispose()
    {
        return EndRecording();
    }

    std::map<std::string, double> FmediaRecorder::GetAmplitude()
    {
        // fmedia不提供实时振幅信息，返回默认值
        return {
            {"current", m_amplitude},
            {"max", m_maxAmplitude}
        };
    }

    std::wstring FmediaRecorder::GetRecordingPath()
    {
        return m_recordingPath;
    }

    HRESULT FmediaRecorder::isEncoderSupported(const std::string encoderName, bool* supported)
    {
        if (encoderName == AudioEncoder().aacLc ||
            encoderName == AudioEncoder().aacHe ||
            encoderName == AudioEncoder().flac ||
            encoderName == AudioEncoder().opus ||
            encoderName == AudioEncoder().wav)
        {
            *supported = true;
        }
        else
        {
            *supported = false;
        }
        return S_OK;
    }

    void FmediaRecorder::UpdateState(RecordState state)
    {
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_recordState = state;
        }

        if (m_stateEventHandler)
        {
            RecordWindowsPlugin::RunOnMainThread([this, state]() -> void {
                m_stateEventHandler->Success(std::make_unique<flutter::EncodableValue>(state));
            });
        }
    }

    std::wstring FmediaRecorder::GetFmediaPath()
    {
        // 获取当前exe路径
        wchar_t exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);
        
        // 获取目录路径
        std::wstring dirPath = exePath;
        size_t pos = dirPath.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
        {
            dirPath = dirPath.substr(0, pos);
        }

        // 构建fmedia路径
        std::wstring fmediaPath = dirPath + L"\\" + FMEDIA_PATH + L"\\" + FMEDIA_BIN;
        return fmediaPath;
    }

    std::wstring FmediaRecorder::GetFileNameSuffix(const std::string& encoderName)
    {
        if (encoderName == AudioEncoder().aacLc || encoderName == AudioEncoder().aacHe)
        {
            return L".m4a";
        }
        else if (encoderName == AudioEncoder().flac)
        {
            return L".flac";
        }
        else if (encoderName == AudioEncoder().opus)
        {
            return L".opus";
        }
        else if (encoderName == AudioEncoder().wav)
        {
            return L".wav";
        }
        else
        {
            return L".m4a";
        }
    }

    std::vector<std::wstring> FmediaRecorder::GetEncoderSettings(const std::string& encoderName, int bitRate)
    {
        if (encoderName == AudioEncoder().aacLc)
        {
            auto quality = GetAacQuality(bitRate);
            std::vector<std::wstring> settings = { L"--aac-profile=LC" };
            settings.insert(settings.end(), quality.begin(), quality.end());
            return settings;
        }
        else if (encoderName == AudioEncoder().aacHe)
        {
            auto quality = GetAacQuality(bitRate);
            std::vector<std::wstring> settings = { L"--aac-profile=HEv2" };
            settings.insert(settings.end(), quality.begin(), quality.end());
            return settings;
        }
        else if (encoderName == AudioEncoder().flac)
        {
            return { L"--flac-compression=6", L"--format=int16" };
        }
        else if (encoderName == AudioEncoder().opus)
        {
            int rate = std::max(6, std::min(510, bitRate / 1000));
            return { L"--opus.bitrate=" + std::to_wstring(rate) };
        }
        else if (encoderName == AudioEncoder().wav)
        {
            return {};
        }
        else
        {
            return {};
        }
    }

    std::vector<std::wstring> FmediaRecorder::GetAacQuality(int bitRate)
    {
        int quality = std::max(8, std::min(800, bitRate / 1000));
        return { L"--aac-quality=" + std::to_wstring(quality) };
    }

    HRESULT FmediaRecorder::CallFmedia(const std::vector<std::wstring>& arguments)
    {
        std::wstring fmediaPath = GetFmediaPath();
        
        // 检查fmedia.exe是否存在
        if (!PathFileExists(fmediaPath.c_str()))
        {
            return E_FAIL;
        }

        // 构建命令行
        std::wstring cmdLine = L"\"" + fmediaPath + L"\" --globcmd.pipe-name=" + PIPE_PROC_NAME;
        for (const auto& arg : arguments)
        {
            cmdLine += L" " + arg;
        }

        STARTUPINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        // 创建进程
        if (!CreateProcess(
            NULL,
            const_cast<wchar_t*>(cmdLine.c_str()),
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            NULL,
            &si,
            &m_processInfo))
        {
            return E_FAIL;
        }

        m_processRunning = true;
        return S_OK;
    }

    HRESULT FmediaRecorder::EndRecording()
    {
        HRESULT hr = S_OK;

        if (m_processRunning)
        {
            // 发送停止和退出命令
            CallFmedia({ L"--globcmd=stop" });
            CallFmedia({ L"--globcmd=quit" });

            // 等待进程结束
            if (m_processInfo.hProcess)
            {
                WaitForSingleObject(m_processInfo.hProcess, 5000); // 等待5秒
                CloseHandle(m_processInfo.hProcess);
                CloseHandle(m_processInfo.hThread);
                ZeroMemory(&m_processInfo, sizeof(m_processInfo));
            }

            m_processRunning = false;
        }

        UpdateState(RecordState::stop);
        m_pConfig = nullptr;
        m_recordingPath.clear();

        return hr;
    }
} 