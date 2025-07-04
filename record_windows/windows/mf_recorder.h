#pragma once

#include <windows.h>
#include <mfidl.h>
#include <mfapi.h>
#include <mferror.h>
#include <shlwapi.h>
#include <Mfreadwrite.h>

#include <assert.h>

// utility functions
#include "utils.h"

#include "record_config.h"
#include "event_stream_handler.h"
#include "recorder_interface.h"

using namespace flutter;

namespace record_windows
{
    // 基于MediaFoundation的录音器实现（用于Windows 10+）
    class MediaFoundationRecorder : public IRecorder, public IMFSourceReaderCallback
    {
    public:
        MediaFoundationRecorder(EventStreamHandler<>* stateEventHandler, EventStreamHandler<>* recordEventHandler);
        virtual ~MediaFoundationRecorder();

        // IRecorder接口实现
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
        
        // IUnknown methods
        STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
        STDMETHODIMP_(ULONG) AddRef();
        STDMETHODIMP_(ULONG) Release();

        // IMFSourceReaderCallback methods
        STDMETHODIMP OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex, DWORD dwStreamFlags, LONGLONG llTimestamp, IMFSample* pSample);
        STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*);
        STDMETHODIMP OnFlush(DWORD);

    private:
        HRESULT CreateAudioCaptureDevice(LPCWSTR pszEndPointID);
        HRESULT CreateSourceReaderAsync();
        HRESULT CreateSinkWriter(std::wstring path);
        HRESULT CreateAudioProfileIn( IMFMediaType** ppMediaType);
        HRESULT CreateAudioProfileOut( IMFMediaType** ppMediaType);

        HRESULT CreateACCProfile( IMFMediaType* pMediaType);
        HRESULT CreateFlacProfile( IMFMediaType* pMediaType);
        HRESULT CreateAmrNbProfile( IMFMediaType* pMediaType);
        HRESULT CreatePcmProfile( IMFMediaType* pMediaType);
        HRESULT FillWavHeader();

        HRESULT InitRecording(std::unique_ptr<RecordConfig> config);
        void UpdateState(RecordState state);
        HRESULT EndRecording();
        void GetAmplitudeFromSample(BYTE* chunk, DWORD size, int bytesPerSample);
        std::vector<int16_t> convertBytesToInt16(BYTE* bytes, DWORD size);

        long                m_nRefCount;        // Reference count.
        CritSec				m_critsec;

        IMFMediaSource* m_pSource;
        IMFPresentationDescriptor* m_pPresentationDescriptor;
        IMFSourceReader* m_pReader;
        IMFSinkWriter* m_pWriter;
        std::wstring m_recordingPath;
        bool m_mfStarted = false;
        IMFMediaType* m_pMediaType;

        bool m_bFirstSample = true;
        LONGLONG m_llBaseTime = 0;
        LONGLONG m_llLastTime = 0;

        double m_amplitude = -160;
        double m_maxAmplitude = -160;
        DWORD m_dataWritten = 0;

        EventStreamHandler<>* m_stateEventHandler;
        EventStreamHandler<>* m_recordEventHandler;

        RecordState m_recordState = RecordState::stop;
        std::unique_ptr<RecordConfig> m_pConfig;
    };
}; 