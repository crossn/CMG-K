#include "KrecParser.hpp"

#include <cstring>
#include <fstream>

namespace KailleraExport
{

static std::string readCStringField(const uint8_t* source, size_t length)
{
    size_t end = 0;
    while (end < length && source[end] != 0)
    {
        ++end;
    }

    return std::string(reinterpret_cast<const char*>(source), end);
}

static std::string readPlayerName(const uint8_t* source, size_t length)
{
    std::string name = readCStringField(source, length);
    for (size_t i = 0; i < name.size();)
    {
        const uint8_t lead = static_cast<uint8_t>(name[i]);
        size_t sequenceLength = 1;
        if ((lead & 0xE0U) == 0xC0U)
        {
            sequenceLength = 2;
        }
        else if ((lead & 0xF0U) == 0xE0U)
        {
            sequenceLength = 3;
        }
        else if ((lead & 0xF8U) == 0xF0U)
        {
            sequenceLength = 4;
        }

        if (sequenceLength > 1 && i + sequenceLength > name.size())
        {
            name.resize(i);
            break;
        }

        bool validContinuation = true;
        for (size_t j = 1; j < sequenceLength; ++j)
        {
            if ((static_cast<uint8_t>(name[i + j]) & 0xC0U) != 0x80U)
            {
                validContinuation = false;
                break;
            }
        }
        i += validContinuation ? sequenceLength : 1;
    }
    return name;
}

static bool fail(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}

bool ParseKrecFile(const std::filesystem::path& path, KrecData& outData, std::string* errorMessage)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return fail(errorMessage, "Cannot open recording file");
    }

    const std::streamoff fileLength = file.tellg();
    if (fileLength < 272)
    {
        return fail(errorMessage, "Recording file is too short");
    }

    file.seekg(0, std::ios::beg);
    if (!file)
    {
        return fail(errorMessage, "Failed to seek recording file");
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(fileLength));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size())))
    {
        return fail(errorMessage, "Failed to read recording file");
    }

    const std::string magic = readCStringField(buffer.data(), 4);
    const bool isKrc1 = (magic == "KRC1");
    const bool isKrc0 = (magic == "KRC0");
    if (!isKrc0 && !isKrc1)
    {
        return fail(errorMessage, "Recording file is not a supported KRC0/KRC1 file");
    }

    const size_t headerSize = isKrc1 ? 400 : 272;
    if (buffer.size() < headerSize)
    {
        return fail(errorMessage, "Recording file header is incomplete");
    }

    outData = {};
    outData.header.magic = magic;
    outData.header.appName = readCStringField(buffer.data() + 4, 128);
    outData.header.gameName = readCStringField(buffer.data() + 132, 128);
    memcpy(&outData.header.timestamp, buffer.data() + 260, sizeof(uint32_t));
    memcpy(&outData.header.playerNumber, buffer.data() + 264, sizeof(int32_t));
    memcpy(&outData.header.numPlayers, buffer.data() + 268, sizeof(int32_t));

    if (isKrc1)
    {
        for (int i = 0; i < 4; ++i)
        {
            outData.header.playerNames[static_cast<size_t>(i)] =
                readPlayerName(buffer.data() + 272 + i * 32, 32);
        }
    }

    int numPlayers = outData.header.numPlayers;
    if (numPlayers < 1)
    {
        numPlayers = 1;
    }
    if (numPlayers > 4)
    {
        numPlayers = 4;
    }

    const int bytesPerFrame = numPlayers * 4;
    bool inDelayPeriod = true;

    const uint8_t* scan = buffer.data() + headerSize;
    const uint8_t* end = buffer.data() + buffer.size();

    while (scan + 1 < end)
    {
        const uint8_t type = *scan++;

        if (type == 0x12)
        {
            if (scan + 2 > end)
            {
                break;
            }

            uint16_t recordLength = 0;
            memcpy(&recordLength, scan, sizeof(uint16_t));
            scan += 2;

            if (recordLength > 0)
            {
                if (scan + recordLength > end)
                {
                    break;
                }

                inDelayPeriod = false;
                outData.inputData.insert(outData.inputData.end(), scan, scan + recordLength);
                scan += recordLength;
            }
            else
            {
                if (inDelayPeriod)
                {
                    outData.delayFrames++;
                }
                outData.inputData.insert(outData.inputData.end(), bytesPerFrame, 0);
            }

            outData.totalInputFrames++;
        }
        else if (type == 0x14)
        {
            while (scan < end && *scan != 0)
            {
                ++scan;
            }
            if (scan < end)
            {
                ++scan;
            }
            scan += 4;
        }
        else if (type == 0x08)
        {
            while (scan < end && *scan != 0)
            {
                ++scan;
            }
            if (scan < end)
            {
                ++scan;
            }
            while (scan < end && *scan != 0)
            {
                ++scan;
            }
            if (scan < end)
            {
                ++scan;
            }
        }
        else
        {
            break;
        }
    }

    if (outData.totalInputFrames <= 0)
    {
        return fail(errorMessage, "Recording file does not contain any input frames");
    }

    return true;
}

} // namespace KailleraExport
