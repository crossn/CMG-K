#include "FfmpegEncoder.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

namespace KailleraExport
{

namespace
{

struct EncoderCandidate
{
    const char* codec;
    bool hardware;
};

static constexpr EncoderCandidate kPreferredVideoEncoders[] = {
    { "h264_nvenc", true },
    { "h264_qsv", true },
    { "h264_amf", true },
    { "libx264", false },
};

static bool fail(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}

#ifdef _WIN32
static std::wstring quotePath(const std::filesystem::path& path)
{
    return L"\"" + path.wstring() + L"\"";
}

static bool runCommandWithCapturedOutput(const std::filesystem::path& executable,
                                         const std::wstring& command,
                                         DWORD timeoutMs,
                                         bool requireOutput,
                                         DWORD* exitCode)
{
    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if (!CreatePipe(&readHandle, &writeHandle, &securityAttributes, 0))
    {
        return false;
    }

    SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = writeHandle;
    startupInfo.hStdError = writeHandle;

    PROCESS_INFORMATION processInfo = {};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    const BOOL created = CreateProcessW(executable.c_str(),
                                        mutableCommand.data(),
                                        nullptr,
                                        nullptr,
                                        TRUE,
                                        CREATE_NO_WINDOW,
                                        nullptr,
                                        nullptr,
                                        &startupInfo,
                                        &processInfo);
    CloseHandle(writeHandle);

    if (!created)
    {
        CloseHandle(readHandle);
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, timeoutMs);
    if (waitResult != WAIT_OBJECT_0)
    {
        TerminateProcess(processInfo.hProcess, 1);
    }

    bool gotOutput = false;
    char buffer[256];
    DWORD bytesRead = 0;
    while (ReadFile(readHandle, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
    {
        gotOutput = true;
    }
    CloseHandle(readHandle);

    DWORD localExitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &localExitCode);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    if (exitCode != nullptr)
    {
        *exitCode = localExitCode;
    }

    if (waitResult != WAIT_OBJECT_0)
    {
        return false;
    }

    if (requireOutput && !gotOutput)
    {
        return false;
    }

    return localExitCode == 0;
}

static bool probeVideoEncoder(const std::filesystem::path& ffmpegPath, const char* codec)
{
    // AMD AMF requires at least 128x96 to initialize; 64x64 silently fails on
    // AMD systems even when the driver is fine. 320x240 clears all known
    // hardware-encoder minimums (NVENC, QSV, AMF). yuv420p is forced because
    // the real export pipeline converts to it and some hardware encoders
    // refuse lavfi's default surface format. The 10s timeout covers AMF's
    // cold-init latency on first launch.
    std::wstring codecWide(codec, codec + strlen(codec));
    const std::wstring command = quotePath(ffmpegPath) +
        L" -v error -f lavfi -i color=black:s=320x240:r=30:d=0.1 -frames:v 1 "
        L"-pix_fmt yuv420p -c:v " + codecWide + L" -f null -";

    return runCommandWithCapturedOutput(ffmpegPath, command, 10000, false, nullptr);
}

static std::string chooseVideoEncoder(const std::filesystem::path& ffmpegPath, bool* hardwareAccelerated)
{
    for (const EncoderCandidate& candidate : kPreferredVideoEncoders)
    {
        if (probeVideoEncoder(ffmpegPath, candidate.codec))
        {
            if (hardwareAccelerated != nullptr)
            {
                *hardwareAccelerated = candidate.hardware;
            }
            return candidate.codec;
        }
    }

    if (hardwareAccelerated != nullptr)
    {
        *hardwareAccelerated = false;
    }
    return {};
}

static std::string buildVideoEncoderFlags(const std::string& videoEncoder, int crf)
{
    char buffer[256];

    if (videoEncoder == "h264_nvenc")
    {
        snprintf(buffer,
                 sizeof(buffer),
                 "-c:v h264_nvenc -preset fast -rc vbr -cq %d -pix_fmt yuv420p",
                 crf);
        return buffer;
    }

    if (videoEncoder == "h264_amf")
    {
        snprintf(buffer,
                 sizeof(buffer),
                 "-c:v h264_amf -quality quality -rc cqp -qp_i %d -qp_p %d -pix_fmt yuv420p",
                 crf,
                 crf);
        return buffer;
    }

    if (videoEncoder == "h264_qsv")
    {
        snprintf(buffer,
                 sizeof(buffer),
                 "-c:v h264_qsv -global_quality %d -look_ahead 0",
                 crf);
        return buffer;
    }

    snprintf(buffer,
             sizeof(buffer),
             "-c:v libx264 -preset ultrafast -crf %d -pix_fmt yuv420p",
             crf);
    return buffer;
}
#endif

} // namespace

bool CheckFfmpegExecutable(const std::filesystem::path& ffmpegPath, std::string* errorMessage)
{
#ifdef _WIN32
    const std::wstring command = quotePath(ffmpegPath) + L" -version";

    DWORD exitCode = 1;
    if (!runCommandWithCapturedOutput(ffmpegPath, command, 5000, true, &exitCode) || exitCode != 0)
    {
        return fail(errorMessage, "FFmpeg was not found");
    }

    if (chooseVideoEncoder(ffmpegPath, nullptr).empty())
    {
        return fail(errorMessage, "FFmpeg does not provide a usable H.264 encoder for MP4 export");
    }

    return true;
#else
    (void)ffmpegPath;
    return fail(errorMessage, "Replay export is only supported on Windows");
#endif
}

static bool runFfmpegCommand(const std::filesystem::path& ffmpegPath,
                             const std::wstring& command,
                             std::string* errorMessage)
{
#ifdef _WIN32
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION processInfo = {};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    if (!CreateProcessW(ffmpegPath.c_str(),
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startupInfo,
                        &processInfo))
    {
        return fail(errorMessage, "Failed to run FFmpeg");
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    if (exitCode != 0)
    {
        return fail(errorMessage, "FFmpeg exited with an error");
    }

    return true;
#else
    (void)ffmpegPath;
    (void)command;
    return fail(errorMessage, "Replay export is only supported on Windows");
#endif
}

bool MuxVideoAndAudio(const std::filesystem::path& ffmpegPath,
                      const std::filesystem::path& videoPath,
                      const std::filesystem::path& audioPath,
                      unsigned int audioFrequency,
                      unsigned long long audioBytes,
                      int capturedFrames,
                      double fps,
                      const std::filesystem::path& outputPath,
                      std::string* errorMessage)
{
#ifdef _WIN32
    const double audioDuration = static_cast<double>(audioBytes) /
                                 (static_cast<double>(audioFrequency) * 4.0);
    const double videoDuration = static_cast<double>(capturedFrames) / fps;
    const double timestampScale = (audioDuration > 0.0 && videoDuration > 0.0)
        ? audioDuration / videoDuration
        : 1.0;

    wchar_t numericArguments[128];
    swprintf(numericArguments,
             sizeof(numericArguments) / sizeof(numericArguments[0]),
             L" -v error -y -itsscale %g -i ",
             timestampScale);
    const std::wstring command = quotePath(ffmpegPath) + numericArguments + quotePath(videoPath) +
        L" -f s16le -ar " + std::to_wstring(audioFrequency) + L" -ac 2 -i " + quotePath(audioPath) +
        L" -c:v copy -c:a aac -strict -2 -b:a 192k -shortest " + quotePath(outputPath);

    return runFfmpegCommand(ffmpegPath, command, errorMessage);
#else
    (void)ffmpegPath;
    (void)videoPath;
    (void)audioPath;
    (void)audioFrequency;
    (void)audioBytes;
    (void)capturedFrames;
    (void)fps;
    (void)outputPath;
    return fail(errorMessage, "Replay export is only supported on Windows");
#endif
}

bool FfmpegEncoder::open(const FfmpegEncoderConfig& config, std::string* errorMessage)
{
    m_FrameWidth = config.width;
    m_FrameHeight = config.height;
    m_ProcessHandle = nullptr;
    m_SelectedVideoEncoder.clear();
    m_HardwareAccelerated = false;

#ifdef _WIN32
    m_SelectedVideoEncoder = config.videoEncoder.empty()
        ? chooseVideoEncoder(config.ffmpegPath, &m_HardwareAccelerated)
        : config.videoEncoder;
    if (m_SelectedVideoEncoder.empty())
    {
        return fail(errorMessage, "FFmpeg does not provide a usable H.264 encoder for MP4 export");
    }
    if (!config.videoEncoder.empty())
    {
        m_HardwareAccelerated = (config.videoEncoder != "libx264");
    }

    const std::string encoderFlags = buildVideoEncoderFlags(m_SelectedVideoEncoder, config.crf);
    fprintf(stderr,
            "Using FFmpeg video encoder: %s%s\n",
            m_SelectedVideoEncoder.c_str(),
            m_HardwareAccelerated ? " (hardware)" : " (CPU)");

    wchar_t videoArguments[256];
    swprintf(videoArguments,
             sizeof(videoArguments) / sizeof(videoArguments[0]),
             L" -v error -y -f rawvideo -pixel_format rgb24 -video_size %dx%d -framerate %g -i pipe:0 ",
             config.width,
             config.height,
             config.fps);
    const std::wstring encoderFlagsWide(encoderFlags.begin(), encoderFlags.end());
    const std::wstring command = quotePath(config.ffmpegPath) + videoArguments +
        encoderFlagsWide + L" " + quotePath(config.outputPath);

    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if (!CreatePipe(&readHandle, &writeHandle, &securityAttributes, 0))
    {
        return fail(errorMessage, "Failed to create FFmpeg stdin pipe");
    }

    SetHandleInformation(writeHandle, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = readHandle;
    startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION processInfo = {};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(config.ffmpegPath.c_str(),
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startupInfo,
                        &processInfo))
    {
        CloseHandle(readHandle);
        CloseHandle(writeHandle);
        return fail(errorMessage, "Failed to launch FFmpeg encoder");
    }

    CloseHandle(readHandle);
    CloseHandle(processInfo.hThread);
    m_ProcessHandle = processInfo.hProcess;

    const int fileDescriptor = _open_osfhandle(reinterpret_cast<intptr_t>(writeHandle), 0);
    if (fileDescriptor == -1)
    {
        CloseHandle(writeHandle);
        WaitForSingleObject(reinterpret_cast<HANDLE>(m_ProcessHandle), 1000);
        CloseHandle(reinterpret_cast<HANDLE>(m_ProcessHandle));
        m_ProcessHandle = nullptr;
        return fail(errorMessage, "Failed to open FFmpeg pipe");
    }

    m_Pipe = _fdopen(fileDescriptor, "wb");
    if (m_Pipe == nullptr)
    {
        _close(fileDescriptor);
        WaitForSingleObject(reinterpret_cast<HANDLE>(m_ProcessHandle), 1000);
        CloseHandle(reinterpret_cast<HANDLE>(m_ProcessHandle));
        m_ProcessHandle = nullptr;
        return fail(errorMessage, "Failed to wrap FFmpeg pipe");
    }

    setvbuf(m_Pipe, nullptr, _IOFBF, 1 << 20);

    return true;
#else
    (void)config;
    return fail(errorMessage, "Replay export is only supported on Windows");
#endif
}

bool FfmpegEncoder::writeFrame(const uint8_t* rgbData, int width, int height, std::string* errorMessage)
{
    if (m_Pipe == nullptr)
    {
        return fail(errorMessage, "FFmpeg encoder pipe is not open");
    }

    const size_t frameSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3U;
    if (fwrite(rgbData, 1, frameSize, m_Pipe) != frameSize)
    {
        return fail(errorMessage, "Failed to write frame data to FFmpeg");
    }

    return true;
}

void FfmpegEncoder::close()
{
    if (m_Pipe != nullptr)
    {
        fclose(m_Pipe);
        m_Pipe = nullptr;
    }

#ifdef _WIN32
    if (m_ProcessHandle != nullptr)
    {
        WaitForSingleObject(reinterpret_cast<HANDLE>(m_ProcessHandle), INFINITE);
        CloseHandle(reinterpret_cast<HANDLE>(m_ProcessHandle));
        m_ProcessHandle = nullptr;
    }
#endif
}

bool FfmpegEncoder::isHardwareAccelerated() const
{
    return m_HardwareAccelerated;
}

const std::string& FfmpegEncoder::selectedVideoEncoder() const
{
    return m_SelectedVideoEncoder;
}

} // namespace KailleraExport
