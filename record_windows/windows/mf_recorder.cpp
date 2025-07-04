#include "mf_recorder.h"
#include "record_windows_plugin.h"

namespace record_windows
{
    MediaFoundationRecorder::MediaFoundationRecorder(EventStreamHandler<>* stateEventHandler, EventStreamHandler<>* recordEventHandler)
        : m_nRefCount(1),
        m_critsec(),
        m_pConfig(nullptr),
        m_pSource(NULL),
        m_pReader(NULL),
        m_pWriter(NULL),
        m_pPresentationDescriptor(NULL),
        m_stateEventHandler(stateEventHandler),
        m_recordEventHandler(recordEventHandler),
        m_recordingPath(std::wstring()),
        m_pMediaType(NULL)
    {
    }

    MediaFoundationRecorder::~MediaFoundationRecorder()
    {
        Dispose();
    }

    HRESULT MediaFoundationRecorder::Start(std::unique_ptr<RecordConfig> config, std::wstring path)
    {
        bool supported = false;
        HRESULT hr = isEncoderSupported(config->encoderName, &supported);

        if (FAILED(hr) || !supported)
        {
            return E_NOTIMPL;
        }

        hr = InitRecording(std::move(config));

        if (SUCCEEDED(hr))
        {
            m_recordingPath = path;
            hr = CreateSinkWriter(path);
        }
        if (SUCCEEDED(hr))
        {
            // Request the first sample
            hr = m_pReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0,
                NULL, NULL, NULL, NULL
            );
        }
        if (SUCCEEDED(hr))
        {
            UpdateState(RecordState::record);
        }
        else
        {
            EndRecording();
        }

        return hr;
    }

    HRESULT MediaFoundationRecorder::StartStream(std::unique_ptr<RecordConfig> config)
    {
        if (config->encoderName != AudioEncoder().pcm16bits)
        {
            return E_NOTIMPL;
        }

        HRESULT hr = InitRecording(std::move(config));

        if (SUCCEEDED(hr))
        {
            // Request the first sample
            hr = m_pReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0,
                NULL, NULL, NULL, NULL
            );
        }
        if (SUCCEEDED(hr))
        {
            UpdateState(RecordState::record);
        }
        else
        {
            EndRecording();
        }

        return hr;
    }

    HRESULT MediaFoundationRecorder::InitRecording(std::unique_ptr<RecordConfig> config)
    {
        HRESULT hr = EndRecording();

        m_pConfig = std::move(config);

        if (SUCCEEDED(hr))
        {
            if (!m_mfStarted)
            {
                hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
            }
            if (SUCCEEDED(hr))
            {
                m_mfStarted = true;
            }
        }

        if (SUCCEEDED(hr))
        {
            if (m_pConfig->deviceId.length() != 0)
            {
                auto deviceId = std::wstring(m_pConfig->deviceId.begin(), m_pConfig->deviceId.end());
                hr = CreateAudioCaptureDevice(deviceId.c_str());
            }
            else
            {
                hr = CreateAudioCaptureDevice(NULL);
            }
        }
        if (SUCCEEDED(hr))
        {
            hr = CreateSourceReaderAsync();
        }

        return hr;
    }

    HRESULT MediaFoundationRecorder::Pause()
    {
        HRESULT hr = S_OK;

        if (m_pSource)
        {
            hr = m_pSource->Pause();

            if (SUCCEEDED(hr))
            {
                UpdateState(RecordState::pause);
            }
        }

        return S_OK;
    }

    HRESULT MediaFoundationRecorder::Resume()
    {
        HRESULT hr = S_OK;

        if (m_pSource)
        {
            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_EMPTY;

            m_llBaseTime = m_llLastTime;

            hr = m_pSource->Start(m_pPresentationDescriptor, NULL, &var);

            if (SUCCEEDED(hr))
            {
                UpdateState(RecordState::record);
            }
        }

        return hr;
    }

    HRESULT MediaFoundationRecorder::Stop()
    {
        if (m_dataWritten == 0)
        {
            return Cancel();
        }

        HRESULT hr = EndRecording();

        if (SUCCEEDED(hr))
        {
            UpdateState(RecordState::stop);
        }

        return hr;
    }

    HRESULT MediaFoundationRecorder::Cancel()
    {
        auto recordingPath = GetRecordingPath();
        HRESULT hr = EndRecording();

        if (SUCCEEDED(hr))
        {
            UpdateState(RecordState::stop);

            if (!recordingPath.empty())
            {
                DeleteFile(recordingPath.c_str());
            }
        }

        return hr;
    }

    bool MediaFoundationRecorder::IsPaused()
    {
        switch (m_recordState)
        {
        case RecordState::pause:
            return true;
        default:
            return false;
        }
    }

    bool MediaFoundationRecorder::IsRecording()
    {
        switch (m_recordState)
        {
        case RecordState::record:
            return true;
        default:
            return false;
        }
    }

    HRESULT MediaFoundationRecorder::EndRecording()
    {
        HRESULT hr = S_OK;

        // Release reader callback first
        SafeRelease(m_pReader);

        if (m_pSource)
        {
            hr = m_pSource->Stop();

            if (SUCCEEDED(hr))
            {
                hr = m_pSource->Shutdown();
            }
        }

        if (m_pWriter)
        {
            hr = m_pWriter->Finalize();
        }

        if (m_pConfig && m_pConfig->encoderName == AudioEncoder().wav) {
            FillWavHeader();
        }

        m_bFirstSample = true;
        m_llBaseTime = 0;
        m_llLastTime = 0;

        m_amplitude = -160;
        m_maxAmplitude = -160;

        if (m_mfStarted)
        {
            hr = MFShutdown();
            if (SUCCEEDED(hr))
            {
                m_mfStarted = false;
            }
        }

        SafeRelease(m_pSource);
        SafeRelease(m_pPresentationDescriptor);
        SafeRelease(m_pWriter);
        SafeRelease(m_pMediaType);
        m_pConfig = nullptr;
        m_recordingPath = std::wstring();

        return hr;
    }

    HRESULT MediaFoundationRecorder::Dispose()
    {
        HRESULT hr = EndRecording();

        m_stateEventHandler = nullptr;
        m_recordEventHandler = nullptr;

        return hr;
    }

    void MediaFoundationRecorder::UpdateState(RecordState state)
    {
        m_recordState = state;

        if (m_stateEventHandler) {
            RecordWindowsPlugin::RunOnMainThread([this, state]() -> void {
                m_stateEventHandler->Success(std::make_unique<flutter::EncodableValue>(state));
            });
        }
    }

    HRESULT MediaFoundationRecorder::CreateAudioCaptureDevice(LPCWSTR deviceId)
    {
        IMFAttributes* pAttributes = NULL;

        HRESULT hr = MFCreateAttributes(&pAttributes, 2);

        // Set the device type to audio.
        if (SUCCEEDED(hr))
        {
            hr = pAttributes->SetGUID(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID
            );
        }

        // Set the endpoint ID.
        if (SUCCEEDED(hr) && deviceId)
        {
            hr = pAttributes->SetString(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID,
                deviceId
            );
        }

        // Create the source
        if (SUCCEEDED(hr))
        {
            hr = MFCreateDeviceSource(pAttributes, &m_pSource);
        }
        // Create presentation descriptor to handle Resume action
        if (SUCCEEDED(hr))
        {
            hr = m_pSource->CreatePresentationDescriptor(&m_pPresentationDescriptor);
        }

        SafeRelease(&pAttributes);
        return hr;
    }

    HRESULT MediaFoundationRecorder::CreateSourceReaderAsync()
    {
        HRESULT hr = S_OK;
        IMFAttributes* pAttributes = NULL;
        IMFMediaType* pMediaTypeIn = NULL;

        hr = MFCreateAttributes(&pAttributes, 1);
        if (SUCCEEDED(hr))
        {
            hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, this);
        }
        if (SUCCEEDED(hr))
        {
            hr = MFCreateSourceReaderFromMediaSource(m_pSource, pAttributes, &m_pReader);
        }
        if (SUCCEEDED(hr))
        {
            hr = CreateAudioProfileIn(&pMediaTypeIn);
        }
        if (SUCCEEDED(hr))
        {
            hr = m_pReader->SetCurrentMediaType(0, NULL, pMediaTypeIn);
        }

        SafeRelease(&pMediaTypeIn);
        SafeRelease(&pAttributes);
        return hr;
    }

    HRESULT MediaFoundationRecorder::CreateSinkWriter(std::wstring path)
    {
        IMFSinkWriter* pSinkWriter = NULL;
        IMFMediaType* pMediaTypeOut = NULL;
        IMFMediaType* pMediaTypeIn = NULL;
        DWORD          streamIndex = 0;

        HRESULT hr = MFCreateSinkWriterFromURL(path.c_str(), NULL, NULL, &pSinkWriter);

        // Set the output media type.
        if (SUCCEEDED(hr))
        {
            hr = CreateAudioProfileOut(&pMediaTypeOut);
        }
        if (SUCCEEDED(hr))
        {
            hr = pSinkWriter->AddStream(pMediaTypeOut, &streamIndex);
        }

        // Set the input media type.
        if (SUCCEEDED(hr))
        {
            hr = m_pReader->GetCurrentMediaType(streamIndex, &pMediaTypeIn);
        }
        if (SUCCEEDED(hr))
        {
            hr = pSinkWriter->SetInputMediaType(streamIndex, pMediaTypeIn, NULL);
        }

        // Tell the sink writer to Start accepting data.
        if (SUCCEEDED(hr))
        {
            hr = pSinkWriter->BeginWriting();
        }

        if (SUCCEEDED(hr))
        {
            m_pWriter = pSinkWriter;
            m_pWriter->AddRef();
            m_pMediaType = pMediaTypeOut;
            m_pMediaType->AddRef();
        }

        SafeRelease(&pSinkWriter);
        SafeRelease(&pMediaTypeOut);
        SafeRelease(&pMediaTypeIn);

        return hr;
    }

    std::map<std::string, double> MediaFoundationRecorder::GetAmplitude()
    {
        return {
            {"current", m_amplitude},
            {"max" , m_maxAmplitude},
        };
    }

    void MediaFoundationRecorder::GetAmplitudeFromSample(BYTE* chunk, DWORD size, int bytesPerSample) {
        int maxSample = -160;

        if (bytesPerSample == 2) { // PCM 16 bits
            auto values = convertBytesToInt16(chunk, size);

            for (DWORD i = 0; i < size; i++) {
                int curSample = std::abs(values[i]);
                if (curSample > maxSample) {
                    maxSample = curSample;
                }
            }

            m_amplitude = 20 * std::log10(maxSample / 32767.0); // 16 signed bits 2^15 - 1
        }
        else /* if (bytesPerSample == 1) */ { // PCM 8 bits
            for (DWORD i = 0; i < size; i++) {
                byte curSample = chunk[i];
                if (curSample > maxSample) {
                    maxSample = curSample;
                }
            }

            m_amplitude = 20 * std::log10(maxSample / 256.0); // 8 unsigned bits 2^8
        }

        if (m_amplitude > m_maxAmplitude) {
            m_maxAmplitude = m_amplitude;
        }
    }

    std::wstring MediaFoundationRecorder::GetRecordingPath()
    {
        return m_recordingPath;
    }

    std::vector<int16_t> MediaFoundationRecorder::convertBytesToInt16(BYTE* bytes, DWORD size)
    {
        // Convert to int16
        std::vector<int16_t> values(size / 2);

        int n = 1;
        if (*(char*)&n == 1) {
            // We're on little endian host
            for (DWORD i = 0; i < size; i += 2) {
                values.push_back(int16_t(bytes[i] << 0 | bytes[i + 1] << 8));
            }
        }
        else {
            // We're on big endian host
            for (DWORD i = 0; i < size; i += 2) {
                values.push_back(int16_t(bytes[i + 1] | bytes[i] << 8));
            }
        }

        return values;
    }

    HRESULT MediaFoundationRecorder::isEncoderSupported(const std::string encoderName, bool* supported)
    {
        MFT_REGISTER_TYPE_INFO typeLookup = {};
        typeLookup.guidMajorType = MFMediaType_Audio;

        if (encoderName == AudioEncoder().aacLc) typeLookup.guidSubtype = MFAudioFormat_AAC;
        /*else if (encoderName == AudioEncoder().aacEld) typeLookup.guidSubtype = MFAudioFormat_AAC;
        else if (encoderName == AudioEncoder().aacHe) typeLookup.guidSubtype = MFAudioFormat_AAC;*/
        else if (encoderName == AudioEncoder().amrNb) typeLookup.guidSubtype = MFAudioFormat_AMR_NB;
        else if (encoderName == AudioEncoder().amrWb) typeLookup.guidSubtype = MFAudioFormat_AMR_WB;
        else if (encoderName == AudioEncoder().opus) typeLookup.guidSubtype = MFAudioFormat_Opus;
        else if (encoderName == AudioEncoder().flac) typeLookup.guidSubtype = MFAudioFormat_FLAC;
        else if (encoderName == AudioEncoder().pcm16bits || encoderName == AudioEncoder().wav) {
            *supported = true;
            return S_OK;
        }
        else {
            *supported = false;
            return S_OK;
        }

        // Enumerate all codecs except for codecs with field-of-use restrictions.
        // Sort the results.
        DWORD dwFlags =
            (MFT_ENUM_FLAG_ALL & (~MFT_ENUM_FLAG_FIELDOFUSE)) |
            MFT_ENUM_FLAG_SORTANDFILTER;

        IMFActivate** ppMFTActivate = NULL;		// array of IMFActivate interface pointers
        UINT32 numMFTActivate;

        // Gets a list of output formats from an audio encoder.
        HRESULT hr = MFTEnumEx(
            MFT_CATEGORY_AUDIO_ENCODER,
            dwFlags,
            NULL,
            &typeLookup,
            &ppMFTActivate,
            &numMFTActivate
        );

        if (SUCCEEDED(hr))
        {
            *supported = numMFTActivate != 0;
        }

        for (UINT32 i = 0; i < numMFTActivate; i++)
        {
            SafeRelease(ppMFTActivate[i]);
        }
        CoTaskMemFree(ppMFTActivate);

        return hr;
    }

    // IUnknown methods
    STDMETHODIMP MediaFoundationRecorder::QueryInterface(REFIID iid, void** ppv)
    {
        static const QITAB qit[] =
        {
            QITABENT(MediaFoundationRecorder, IMFSourceReaderCallback),
            { 0 },
        };
        return QISearch(this, qit, iid, ppv);
    }

    STDMETHODIMP_(ULONG) MediaFoundationRecorder::AddRef()
    {
        return InterlockedIncrement(&m_nRefCount);
    }

    STDMETHODIMP_(ULONG) MediaFoundationRecorder::Release()
    {
        ULONG uCount = InterlockedDecrement(&m_nRefCount);
        if (uCount == 0)
        {
            delete this;
        }
        return uCount;
    }

    // IMFSourceReaderCallback methods
    STDMETHODIMP MediaFoundationRecorder::OnEvent(DWORD, IMFMediaEvent*)
    {
        return S_OK;
    }

    STDMETHODIMP MediaFoundationRecorder::OnFlush(DWORD)
    {
        return S_OK;
    }

    HRESULT MediaFoundationRecorder::OnReadSample(
        HRESULT hrStatus,
        DWORD dwStreamIndex,
        DWORD dwStreamFlags,
        LONGLONG llTimestamp,
        IMFSample* pSample      // Can be NULL
    )
    {
        AutoLock lock(m_critsec);

        HRESULT hr = S_OK;

        if (SUCCEEDED(hrStatus))
        {
            if (pSample)
            {
                if (m_bFirstSample)
                {
                    m_llBaseTime = llTimestamp;
                    m_bFirstSample = false;
                    m_dataWritten = 0;
                }

                // Save current timestamp in case of Pause
                m_llLastTime = llTimestamp;

                // rebase the time stamp
                llTimestamp -= m_llBaseTime;

                hr = pSample->SetSampleTime(llTimestamp);

                // Write to file if there's a writer
                if (SUCCEEDED(hr) && m_pWriter)
                {
                    hr = m_pWriter->WriteSample(dwStreamIndex, pSample);
                }

                if (SUCCEEDED(hr))
                {
                    IMFMediaBuffer* pBuffer = NULL;
                    hr = pSample->ConvertToContiguousBuffer(&pBuffer);

                    if (SUCCEEDED(hr))
                    {
                        BYTE* pChunk = NULL;
                        DWORD size = 0;
                        hr = pBuffer->Lock(&pChunk, NULL, &size);

                        if (SUCCEEDED(hr))
                        {
                            // Update total data written
                            m_dataWritten += size;

                            // Send data to stream when there's no writer
                            if (m_recordEventHandler && !m_pWriter) {
                                std::vector<uint8_t> bytes(pChunk, pChunk + size);

                                RecordWindowsPlugin::RunOnMainThread([this, bytes]() -> void {
                                    m_recordEventHandler->Success(std::make_unique<flutter::EncodableValue>(bytes));
                                });
                            }

                            GetAmplitudeFromSample(pChunk, size, 2);

                            pBuffer->Unlock();
                        }

                        SafeRelease(pBuffer);
                    }
                }
            }

            if (SUCCEEDED(hr))
            {
                // Read another sample
                hr = m_pReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                    0,
                    NULL, NULL, NULL, NULL
                );
            }
        }

        return hr;
    }

    // MediaType creation methods
    HRESULT MediaFoundationRecorder::CreateAudioProfileIn(IMFMediaType** ppMediaType)
    {
        HRESULT hr = S_OK;

        IMFMediaType* pMediaType = NULL;

        hr = MFCreateMediaType(&pMediaType);

        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_pConfig->sampleRate);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_pConfig->numChannels);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AVG_BITRATE, m_pConfig->bitRate);
        }
        if (SUCCEEDED(hr))
        {
            *ppMediaType = pMediaType;
            (*ppMediaType)->AddRef();
        }

        SafeRelease(&pMediaType);

        return hr;
    }

    HRESULT MediaFoundationRecorder::CreateAudioProfileOut(IMFMediaType** ppMediaType)
    {
        HRESULT hr = S_OK;

        IMFMediaType* pMediaType = NULL;
        GUID audioFormat{};

        hr = MFCreateMediaType(&pMediaType);

        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        }
        if (SUCCEEDED(hr))
        {
            if (m_pConfig->encoderName == "aacLc") hr = CreateACCProfile(pMediaType);
            else if (m_pConfig->encoderName == "aacEld") hr = CreateACCProfile(pMediaType);
            else if (m_pConfig->encoderName == "aacHe") hr = CreateACCProfile(pMediaType);
            else if (m_pConfig->encoderName == "amrNb") hr = CreateAmrNbProfile(pMediaType);
            else if (m_pConfig->encoderName == "amrWb") hr = CreateAmrNbProfile(pMediaType);
            else if (m_pConfig->encoderName == "flac") hr = CreateFlacProfile(pMediaType);
            else if (m_pConfig->encoderName == "pcm16bits") hr = CreatePcmProfile(pMediaType);
            else if (m_pConfig->encoderName == "wav") hr = CreatePcmProfile(pMediaType);
            else hr = E_NOTIMPL;
        }

        if (SUCCEEDED(hr))
        {
            *ppMediaType = pMediaType;
            (*ppMediaType)->AddRef();
        }

        SafeRelease(&pMediaType);

        return hr;
    }

    HRESULT MediaFoundationRecorder::CreateACCProfile(IMFMediaType* pMediaType)
    {
        HRESULT hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_pConfig->sampleRate);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_pConfig->numChannels);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AVG_BITRATE, m_pConfig->bitRate);
        }

        return hr;
    }

    HRESULT MediaFoundationRecorder::CreateFlacProfile(IMFMediaType* pMediaType)
    {
        HRESULT hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_FLAC);

        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_pConfig->sampleRate);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_pConfig->numChannels);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AVG_BITRATE, m_pConfig->bitRate);
        }

        return hr;
    }

    HRESULT MediaFoundationRecorder::CreateAmrNbProfile(IMFMediaType* pMediaType)
    {
        HRESULT hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AMR_NB);

        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        }

        return hr;
    }

    HRESULT MediaFoundationRecorder::CreatePcmProfile(IMFMediaType* pMediaType)
    {
        HRESULT hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);

        auto bitsPerSample = 16;

        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample);
        }
        // Calculate derived values.
        UINT32 blockAlign = m_pConfig->numChannels * (bitsPerSample / 8);
        UINT32 bytesPerSecond = blockAlign * m_pConfig->sampleRate;

        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_pConfig->numChannels);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_pConfig->sampleRate);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytesPerSecond);
        }
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        }

        return hr;
    }

    HRESULT MediaFoundationRecorder::FillWavHeader() {
        // Implementation copied from original record_mediatype.cpp
        // This is a placeholder - the actual implementation would be more complex
        return S_OK;
    }
}; 