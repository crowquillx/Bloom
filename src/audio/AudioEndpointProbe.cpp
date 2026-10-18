#include "AudioEndpointProbe.h"

#ifdef Q_OS_WIN

#include <windows.h>

#include <QStringView>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ks.h>
#include <ksmedia.h>

#include <array>
#include <memory>
#include <utility>

namespace {

template<typename T>
using ComPtr = std::unique_ptr<T, void (*)(T *)>;

template<typename T>
ComPtr<T> makeComPtr(T *pointer = nullptr)
{
    return ComPtr<T>(pointer, [](T *value) {
        if (value)
            value->Release();
    });
}

using CoTaskMemString = std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)>;

struct ComApartment {
    ComApartment()
        : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
        , initialized(SUCCEEDED(result))
    {
    }

    ~ComApartment()
    {
        if (initialized)
            CoUninitialize();
    }

    HRESULT result;
    bool initialized;
};

struct CarrierFormat {
    const char *codec;
    DWORD sampleRate;
    WORD channels;
    DWORD channelMask;
    GUID subFormat;
};

struct PcmLayout {
    const char *name;
    WORD channels;
    DWORD channelMask;
};

struct PcmSampleFormat {
    WORD containerBits;
    WORD validBits;
};

constexpr GUID kIec61937Ac3 = {0x00000092, 0x0000, 0x0010,
                               {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kIec61937Dts = {0x00000008, 0x0000, 0x0010,
                               {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kIec61937Eac3 = {0x0000000a, 0x0cea, 0x0010,
                                {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kIec61937TrueHd = {0x0000000c, 0x0cea, 0x0010,
                                  {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kIec61937DtsHd = {0x0000000b, 0x0cea, 0x0010,
                                 {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kPcmSubFormat = {0x00000001, 0x0000, 0x0010,
                                {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr CLSID kMmDeviceEnumeratorClsid = {0xbcde0395, 0xe52f, 0x467c,
                                            {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};

constexpr std::array<CarrierFormat, 5> kCarrierFormats = {{
    {"ac3", 48000, 2, KSAUDIO_SPEAKER_STEREO, kIec61937Ac3},
    {"dts", 48000, 2, KSAUDIO_SPEAKER_STEREO, kIec61937Dts},
    {"eac3", 192000, 2, KSAUDIO_SPEAKER_STEREO, kIec61937Eac3},
    {"truehd", 192000, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND, kIec61937TrueHd},
    {"dts-hd", 192000, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND, kIec61937DtsHd},
}};

constexpr std::array<PcmLayout, 4> kPcmLayouts = {{
    {"stereo", 2, KSAUDIO_SPEAKER_STEREO},
    {"5.1", 6, KSAUDIO_SPEAKER_5POINT1},
    {"5.1(side)", 6, KSAUDIO_SPEAKER_5POINT1_SURROUND},
    {"7.1", 8, KSAUDIO_SPEAKER_7POINT1_SURROUND},
}};

constexpr std::array<DWORD, 3> kPcmSampleRates = {{48000, 96000, 192000}};
constexpr std::array<PcmSampleFormat, 3> kPcmSampleFormats = {{{32, 24}, {32, 32}, {16, 16}}};

QString fromWide(const wchar_t *value)
{
    return QString::fromWCharArray(value);
}

bool tryGetEndpointId(IMMDevice *device, QString *id)
{
    LPWSTR rawId = nullptr;
    if (FAILED(device->GetId(&rawId)) || !rawId)
        return false;

    [[maybe_unused]] CoTaskMemString ownedId(rawId, &CoTaskMemFree);
    *id = fromWide(rawId);
    return !id->isEmpty();
}

QString normalizedId(QString id)
{
    id = id.trimmed().toLower();
    while (id.startsWith(QLatin1Char('{')) && id.endsWith(QLatin1Char('}')))
        id = id.mid(1, id.size() - 2);
    return id;
}

bool isGuidToken(const QString &id)
{
    if (!(id.startsWith(QLatin1Char('{')) && id.endsWith(QLatin1Char('}'))))
        return false;
    const QString body = id.mid(1, id.size() - 2);
    return body.contains(QLatin1Char('-')) || body.contains(QLatin1Char('.'));
}

bool idMatches(const QString &requestedId, const QString &endpointId)
{
    if (requestedId.compare(endpointId, Qt::CaseInsensitive) == 0)
        return true;

    // mpv can expose the MMDevice GUID portion while GetId returns the full
    // SWD endpoint path. Accept that form only when it identifies one GUID token.
    if (!isGuidToken(requestedId))
        return false;

    const QString normalizedEndpoint = endpointId.toLower();
    const QString request = requestedId.toLower();
    int position = normalizedEndpoint.indexOf(request);
    if (position >= 0) {
        return normalizedEndpoint.indexOf(request, position + request.size()) < 0;
    }

    const QString token = normalizedId(requestedId);
    position = normalizedEndpoint.indexOf(token);
    if (position < 0)
        return false;

    const int nextPosition = normalizedEndpoint.indexOf(token, position + token.size());
    return nextPosition < 0;
}

ComPtr<IMMDevice> resolveEndpoint(IMMDeviceEnumerator *enumerator,
                                  const QString &requestedId,
                                  QString *resolvedId)
{
    const QString normalizedRequest = requestedId.trimmed();
    if (normalizedRequest.isEmpty() || normalizedRequest.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
        IMMDevice *rawDevice = nullptr;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &rawDevice)))
            return makeComPtr<IMMDevice>();
        auto endpoint = makeComPtr(rawDevice);
        tryGetEndpointId(endpoint.get(), resolvedId);
        return endpoint;
    }

    constexpr QStringView wasapiPrefix = u"wasapi/";
    if (!normalizedRequest.startsWith(wasapiPrefix, Qt::CaseInsensitive))
        return makeComPtr<IMMDevice>();

    const QString wantedId = normalizedRequest.mid(wasapiPrefix.size()).trimmed();
    if (wantedId.isEmpty())
        return makeComPtr<IMMDevice>();

    IMMDevice *rawDevice = nullptr;
    if (SUCCEEDED(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(wantedId.utf16()), &rawDevice))) {
        auto endpoint = makeComPtr(rawDevice);
        tryGetEndpointId(endpoint.get(), resolvedId);
        return endpoint;
    }

    IMMDeviceCollection *rawCollection = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &rawCollection)))
        return makeComPtr<IMMDevice>();
    auto collection = makeComPtr(rawCollection);

    UINT count = 0;
    if (FAILED(collection->GetCount(&count)))
        return makeComPtr<IMMDevice>();

    ComPtr<IMMDevice> match = makeComPtr<IMMDevice>();
    QString matchId;
    for (UINT index = 0; index < count; ++index) {
        IMMDevice *rawCandidate = nullptr;
        if (FAILED(collection->Item(index, &rawCandidate)))
            continue;
        auto candidate = makeComPtr(rawCandidate);
        QString candidateId;
        if (!tryGetEndpointId(candidate.get(), &candidateId) || !idMatches(wantedId, candidateId))
            continue;
        if (match)
            return makeComPtr<IMMDevice>();
        matchId = std::move(candidateId);
        match = std::move(candidate);
    }

    if (match)
        *resolvedId = std::move(matchId);
    return match;
}

bool endpointIsActive(IMMDevice *endpoint)
{
    DWORD state = 0;
    return SUCCEEDED(endpoint->GetState(&state)) && (state & DEVICE_STATE_ACTIVE) != 0;
}

WAVEFORMATEXTENSIBLE makeCarrierWaveFormat(const CarrierFormat &carrier)
{
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = carrier.channels;
    format.Format.nSamplesPerSec = carrier.sampleRate;
    format.Format.wBitsPerSample = 16;
    format.Format.nBlockAlign = carrier.channels * 2;
    format.Format.nAvgBytesPerSec = carrier.sampleRate * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 16;
    format.dwChannelMask = carrier.channelMask;
    format.SubFormat = carrier.subFormat;
    return format;
}

WAVEFORMATEXTENSIBLE makePcmWaveFormat(const PcmLayout &layout,
                                      DWORD sampleRate,
                                      const PcmSampleFormat &sampleFormat)
{
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = layout.channels;
    format.Format.nSamplesPerSec = sampleRate;
    format.Format.wBitsPerSample = sampleFormat.containerBits;
    format.Format.nBlockAlign = layout.channels * (sampleFormat.containerBits / 8);
    format.Format.nAvgBytesPerSec = sampleRate * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = sampleFormat.validBits;
    format.dwChannelMask = layout.channelMask;
    format.SubFormat = kPcmSubFormat;
    return format;
}

bool supportsPcmLayout(IAudioClient *audioClient, const PcmLayout &layout)
{
    for (const DWORD sampleRate : kPcmSampleRates) {
        for (const PcmSampleFormat &sampleFormat : kPcmSampleFormats) {
            WAVEFORMATEXTENSIBLE format = makePcmWaveFormat(layout, sampleRate, sampleFormat);
            if (audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,
                                               &format.Format,
                                               nullptr) == S_OK) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

#endif

namespace Bloom {

AudioOutputCapabilities probeAudioEndpoint(const QString &device)
{
    AudioOutputCapabilities capabilities;
    capabilities.device = device.trimmed().isEmpty() ? QStringLiteral("auto") : device.trimmed();

#ifdef Q_OS_WIN
    const bool isAuto = capabilities.device.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
    const bool isWasapi = capabilities.device.startsWith(QStringLiteral("wasapi/"), Qt::CaseInsensitive);
    if (!isAuto && !isWasapi) {
        capabilities.endpointId = capabilities.device;
        return capabilities;
    }

    const ComApartment apartment;
    if (!apartment.initialized)
        return capabilities;

    IMMDeviceEnumerator *rawEnumerator = nullptr;
    if (FAILED(CoCreateInstance(kMmDeviceEnumeratorClsid, nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void **>(&rawEnumerator)))) {
        return capabilities;
    }
    auto enumerator = makeComPtr(rawEnumerator);

    auto endpoint = resolveEndpoint(enumerator.get(), capabilities.device, &capabilities.endpointId);
    if (!endpoint || !endpointIsActive(endpoint.get())) {
        capabilities.endpointAvailable = false;
        return capabilities;
    }

    IAudioClient *rawAudioClient = nullptr;
    if (FAILED(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void **>(&rawAudioClient)))) {
        return capabilities;
    }
    auto audioClient = makeComPtr(rawAudioClient);

    capabilities.evidence = QStringLiteral("wasapi-exclusive-format-query");
    for (const CarrierFormat &carrier : kCarrierFormats) {
        WAVEFORMATEXTENSIBLE format = makeCarrierWaveFormat(carrier);
        if (audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &format.Format, nullptr) == S_OK)
            capabilities.passthroughCodecs.append(QString::fromLatin1(carrier.codec));
    }
    for (const PcmLayout &layout : kPcmLayouts) {
        if (supportsPcmLayout(audioClient.get(), layout))
            capabilities.pcmLayouts.append(QString::fromLatin1(layout.name));
    }
#else
    capabilities.endpointId = capabilities.device;
#endif

    return capabilities;
}

} // namespace Bloom
