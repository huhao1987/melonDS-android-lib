#include <oboe/Oboe.h>
#include "MelonDS.h"
#include "FileUtils.h"
#include "OboeCallback.h"
#include "MelonAudioStreamErrorCallback.h"
#include "MicInputOboeCallback.h"
#include "AndroidARCodeFile.h"
#include "../NDS.h"
#include "../GPU.h"
#include "../GPU3D.h"
#include "../GBACart.h"
#include "../SPU.h"
#include "../Platform.h"
#include "../Config.h"
#include "../AREngine.h"
#include "../DSi.h"
#include "../FileSavestate.h"
#include "MemorySavestate.h"
#include "SharedConfig.h"
#include "PlatformConfig.h"
#include "FrontendUtil.h"
#include "RewindManager.h"
#include <android/asset_manager.h>
#include <cstring>

#define MIC_BUFFER_SIZE 2048

oboe::AudioStream *audioStream;
oboe::AudioStreamErrorCallback *audioStreamErrorCallback;
oboe::AudioStream *micInputStream;
OboeCallback *outputCallback;
MicInputOboeCallback *micInputCallback;
AndroidARCodeFile *arCodeFile;

namespace MelonDSAndroid
{
    u32* textureBuffer;
    char* internalFilesDir;
    int volume;
    int audioLatency;
    int micInputType;
    int frame = 0;
    AAssetManager* assetManager;
    AndroidFileHandler* fileHandler;

    // Variables used to keep the current state so that emulation can be reset
    char* currentRomPath = NULL;
    char* currentSramPath = NULL;
    char* currentGbaRomPath = NULL;
    char* currentGbaSramPath = NULL;
    bool currentLoadGbaRom;
    bool currentLoadDirect;
    RunMode currentRunMode;

    void setupAudioOutputStream();
    void cleanupAudioOutputStream();
    void setupMicInputStream();
    void resetAudioOutputStream();
    void copyString(char** dest, const char* source);

    void setup(EmulatorConfiguration emulatorConfiguration, AAssetManager* androidAssetManager, AndroidFileHandler* androidFileHandler, u32* textureBufferPointer) {
        copyString(&internalFilesDir, emulatorConfiguration.internalFilesDir);
        assetManager = androidAssetManager;
        fileHandler = androidFileHandler;
        textureBuffer = textureBufferPointer;

        audioLatency = emulatorConfiguration.audioLatency;
        volume = emulatorConfiguration.volume;
        micInputType = emulatorConfiguration.micSource;

        if (emulatorConfiguration.soundEnabled) {
            setupAudioOutputStream();
        }

        if (micInputType == 2) {
            setupMicInputStream();
        }

        // Internal BIOS and Firmware can only be used for DS
        if (emulatorConfiguration.userInternalFirmwareAndBios) {
            memcpy(Config::FirmwareUsername, emulatorConfiguration.firmwareConfiguration.username, sizeof(emulatorConfiguration.firmwareConfiguration.username));
            memcpy(Config::FirmwareMessage, emulatorConfiguration.firmwareConfiguration.message, sizeof(emulatorConfiguration.firmwareConfiguration.message));
            Config::FirmwareLanguage = emulatorConfiguration.firmwareConfiguration.language;
            Config::FirmwareBirthdayMonth = emulatorConfiguration.firmwareConfiguration.birthdayMonth;
            Config::FirmwareBirthdayDay = emulatorConfiguration.firmwareConfiguration.birthdayDay;
            Config::FirmwareFavouriteColour = emulatorConfiguration.firmwareConfiguration.favouriteColour;
            memcpy(Config::InternalMacAddress, emulatorConfiguration.firmwareConfiguration.macAddress, sizeof(emulatorConfiguration.firmwareConfiguration.macAddress));
            Config::ConsoleType = 0;
            Config::ExternalBIOSEnable = 0;
            NDS::SetConsoleType(0);
        } else {
            // DS BIOS files are always required
            strcpy(Config::BIOS7Path, emulatorConfiguration.dsBios7Path);
            strcpy(Config::BIOS9Path, emulatorConfiguration.dsBios9Path);
            Config::ExternalBIOSEnable = 1;

            if (emulatorConfiguration.consoleType == 0) {
                strcpy(Config::FirmwarePath, emulatorConfiguration.dsFirmwarePath);
                Config::ConsoleType = 0;
                NDS::SetConsoleType(0);
            } else {
                strcpy(Config::DSiBIOS7Path, emulatorConfiguration.dsiBios7Path);
                strcpy(Config::DSiBIOS9Path, emulatorConfiguration.dsiBios9Path);
                strcpy(Config::DSiFirmwarePath, emulatorConfiguration.dsiFirmwarePath);
                strcpy(Config::DSiNANDPath, emulatorConfiguration.dsiNandPath);
                Config::ConsoleType = 1;
                NDS::SetConsoleType(1);
            }
        }

#ifdef JIT_ENABLED
        Config::JIT_Enable = emulatorConfiguration.useJit ? 1 : 0;
#endif

        Config::AudioBitrate = emulatorConfiguration.audioBitrate;
        Config::FirmwareOverrideSettings = false;
        Config::RandomizeMAC = emulatorConfiguration.firmwareConfiguration.randomizeMacAddress ? 1 : 0;
        Config::SocketBindAnyAddr = 1;

        Config::RewindEnabled = emulatorConfiguration.rewindEnabled;
        Config::RewindCaptureSpacingSeconds = emulatorConfiguration.rewindCaptureSpacingSeconds;
        Config::RewindLengthSeconds = emulatorConfiguration.rewindLengthSeconds;
        // Use 10MB per savestate
        RewindManager::SetRewindBufferSizes(1024 * 1024 * 10, 256 * 384 * 4);

        NDS::Init();
        GPU::InitRenderer(0);
        GPU::SetRenderSettings(0, emulatorConfiguration.renderSettings);
        SPU::SetInterpolation(emulatorConfiguration.audioInterpolation);
    }

    void setCodeList(std::list<Cheat> cheats)
    {
        if (arCodeFile == NULL) {
            arCodeFile = new AndroidARCodeFile();
            AREngine::SetCodeFile(arCodeFile);
        }

        ARCodeList codeList;

        for (std::list<Cheat>::iterator it = cheats.begin(); it != cheats.end(); it++)
        {
            Cheat& cheat = *it;

            ARCode code = {
                    .Enabled = cheat.enable,
                    .CodeLen = cheat.codeLength
            };
            memcpy(code.Code, cheat.code, sizeof(code.Code));

            codeList.push_back(code);
        }

        arCodeFile->updateCodeList(codeList);
    }

    void updateEmulatorConfiguration(EmulatorConfiguration emulatorConfiguration) {
        int oldMicSource = micInputType;
        int oldVolume = volume;
        int oldAudioLatency = audioLatency;

        Config::AudioBitrate = emulatorConfiguration.audioBitrate;
        GPU::SetRenderSettings(0, emulatorConfiguration.renderSettings);
        SPU::SetInterpolation(emulatorConfiguration.audioInterpolation);
        Config::RewindEnabled = emulatorConfiguration.rewindEnabled;
        Config::RewindCaptureSpacingSeconds = emulatorConfiguration.rewindCaptureSpacingSeconds;
        Config::RewindLengthSeconds = emulatorConfiguration.rewindLengthSeconds;

        if (emulatorConfiguration.rewindEnabled) {
            RewindManager::TrimRewindWindowIfRequired();
        } else {
            RewindManager::Reset();
        }

        audioLatency = emulatorConfiguration.audioLatency;
        volume = emulatorConfiguration.volume;
        micInputType = emulatorConfiguration.micSource;

        if (emulatorConfiguration.soundEnabled && volume > 0) {
            if (audioStream == NULL) {
                setupAudioOutputStream();
            } else if (oldAudioLatency != audioLatency || oldVolume != volume) {
                // Recreate audio stream with new settings
                cleanupAudioOutputStream();
                setupAudioOutputStream();
            }
        } else if (audioStream != NULL) {
            cleanupAudioOutputStream();
        }

        if (oldMicSource == 2 && micInputType != 2) {
            // No longer using device mic. Destroy stream
            if (micInputStream != NULL) {
                micInputStream->requestStop();
                micInputStream->close();
                delete micInputStream;
                delete micInputCallback;
                micInputStream = NULL;
                micInputCallback = NULL;
            }
        } else if (oldMicSource != 2 && micInputType == 2) {
            // Now using device mic. Setup stream
            setupMicInputStream();
        }
    }

    int loadRom(char* romPath, char* sramPath, bool loadDirect, bool loadGbaRom, char* gbaRom, char* gbaSram)
    {
        copyString(&currentRomPath, romPath);
        copyString(&currentSramPath, sramPath);
        copyString(&currentGbaRomPath, gbaRom);
        copyString(&currentGbaSramPath, gbaSram);
        currentLoadDirect = loadDirect;
        currentLoadGbaRom = loadGbaRom;
        currentRunMode = ROM;

        if (NDS::ConsoleType == 1)
        {
            DSi::CloseDSiNAND();
            if (Frontend::SetupDSiNAND() != Frontend::Load_OK)
            {
                return 3;
            }
        }

        bool loaded = NDS::LoadROM(romPath, sramPath, loadDirect);
        if (!loaded)
            return 2;

        // Slot 2 is not supported in DSi
        if (loadGbaRom && NDS::ConsoleType == 0)
        {
            if (!NDS::LoadGBAROM(gbaRom, gbaSram))
                return 1;
        }

        return 0;
    }

    int bootFirmware()
    {
        currentRunMode = FIRMWARE;
        return Frontend::LoadBIOS();
    }

    void start()
    {
        if (audioStream != NULL)
            audioStream->requestStart();

        if (micInputStream != NULL)
            micInputStream->requestStart();

        frame = 0;
    }

    u32 loop()
    {
        u32 nLines = NDS::RunFrame();

        int frontbuf = GPU::FrontBuffer;
        if (GPU::Framebuffer[frontbuf][0] && GPU::Framebuffer[frontbuf][1])
        {
            memcpy(textureBuffer, GPU::Framebuffer[frontbuf][0], 256 * 192 * 4);
            memcpy(&textureBuffer[256 * 192], GPU::Framebuffer[frontbuf][1], 256 * 192 * 4);
        }
        frame++;

        if (RewindManager::ShouldCaptureState(frame))
        {
            auto nextRewindState = RewindManager::GetNextRewindSaveState(frame);
            saveRewindState(nextRewindState);
        }

        return nLines;
    }

    void pause() {
        if (audioStream != NULL)
            audioStream->requestPause();

        if (micInputStream != NULL)
            micInputStream->requestPause();
    }

    void resume() {
        if (audioStream != NULL)
            audioStream->requestStart();

        if (micInputStream != NULL)
            micInputStream->requestStart();
    }

    bool reset()
    {
        frame = 0;
        if (currentRunMode == ROM) {
            int result = loadRom(currentRomPath, currentSramPath, currentLoadDirect, currentLoadGbaRom, currentGbaRomPath, currentGbaSramPath);
            if (result != 2 && arCodeFile != NULL) {
                AREngine::SetCodeFile(arCodeFile);
            }

            RewindManager::Reset();

            return result != 2;
        } else {
            int result = bootFirmware();
            return result == 0;
        }
    }

    void updateMic()
    {
        switch (micInputType)
        {
            case 0: // no mic
                Frontend::Mic_FeedSilence();
                break;
            case 1: // white noise
                Frontend::Mic_FeedNoise();
                break;
            case 2: // host mic
                Frontend::Mic_FeedExternalBuffer();
                break;
        }
    }

    bool saveState(const char* path)
    {
        FileSavestate* savestate = new FileSavestate(path, true);
        if (savestate->Error)
        {
            delete savestate;
            return false;
        }
        else
        {
            NDS::DoSavestate(savestate);
            delete savestate;
            return true;
        }
    }

    bool loadState(const char* path)
    {
        bool success = true;
        char* backupPath = joinPaths(internalFilesDir, "backup.mln");

        FileSavestate* backup = new FileSavestate(backupPath, true);
        NDS::DoSavestate(backup);
        delete backup;

        FileSavestate* savestate = new FileSavestate(path, false);
        if (savestate->Error)
        {
            delete savestate;

            savestate = new FileSavestate(backupPath, false);
            success = false;
        }

        NDS::DoSavestate(savestate);
        delete savestate;

        // Delete backup file
        remove(backupPath);

        delete[] backupPath;

        return success;
    }

    bool saveRewindState(RewindManager::RewindSaveState rewindSaveState)
    {
        MemorySavestate* savestate = new MemorySavestate(rewindSaveState.buffer, true);
        if (savestate->Error)
        {
            delete savestate;
            return false;
        }
        else
        {
            NDS::DoSavestate(savestate);
            memcpy(rewindSaveState.screenshot, textureBuffer, 256 * 384 * 4);

            delete savestate;
            return true;
        }
    }

    bool loadRewindState(RewindManager::RewindSaveState rewindSaveState)
    {
        bool success = true;
        char* backupPath = joinPaths(internalFilesDir, "backup.mln");

        FileSavestate* backup = new FileSavestate(backupPath, true);
        NDS::DoSavestate(backup);
        delete backup;

        Savestate* savestate = new MemorySavestate(rewindSaveState.buffer, false);
        if (savestate->Error)
        {
            delete savestate;

            savestate = new FileSavestate(backupPath, false);
            success = false;
        }

        NDS::DoSavestate(savestate);
        delete savestate;

        // Delete backup file
        remove(backupPath);
        // Restore frame
        frame = rewindSaveState.frame;
        RewindManager::OnRewindFromState(rewindSaveState);

        delete[] backupPath;

        return success;
    }

    RewindWindow getRewindWindow()
    {
        return RewindWindow {
            .currentFrame = frame,
            .rewindStates = RewindManager::GetRewindWindow()
        };
    }

    void cleanup()
    {
        GBACart::Eject();
        GPU::DeInitRenderer();
        NDS::DeInit();
        RewindManager::Reset();

        if (internalFilesDir) {
            free(internalFilesDir);
            internalFilesDir = NULL;
        }

        free(currentRomPath);
        free(currentSramPath);
        free(currentGbaRomPath);
        free(currentGbaSramPath);
        currentRomPath = NULL;
        currentSramPath = NULL;
        currentGbaRomPath = NULL;
        currentGbaSramPath = NULL;

        cleanupAudioOutputStream();

        if (micInputStream != NULL) {
            micInputStream->requestStop();
            micInputStream->close();
            delete micInputStream;
            delete micInputCallback;
            micInputStream = NULL;
            micInputCallback = NULL;
            Frontend::Mic_SetExternalBuffer(NULL, 0);
        }

        if (arCodeFile != NULL) {
            delete arCodeFile;
            arCodeFile = NULL;
        }

        fileHandler = NULL;
        assetManager = NULL;

        textureBuffer = NULL;
    }

    void setupAudioOutputStream()
    {
        oboe::PerformanceMode performanceMode;
        switch (audioLatency) {
            case 0:
                performanceMode = oboe::PerformanceMode::LowLatency;
                break;
            case 1:
                performanceMode = oboe::PerformanceMode::None;
                break;
            case 2:
                performanceMode = oboe::PerformanceMode::PowerSaving;
                break;
            default:
                performanceMode = oboe::PerformanceMode::None;
        }

        outputCallback = new OboeCallback(volume);
        audioStreamErrorCallback = new MelonAudioStreamErrorCallback(resetAudioOutputStream);
        oboe::AudioStreamBuilder streamBuilder;
        streamBuilder.setChannelCount(2);
        streamBuilder.setFramesPerCallback(1024);
        streamBuilder.setSampleRate(48000);
        streamBuilder.setFormat(oboe::AudioFormat::I16);
        streamBuilder.setDirection(oboe::Direction::Output);
        streamBuilder.setPerformanceMode(performanceMode);
        streamBuilder.setSharingMode(oboe::SharingMode::Shared);
        streamBuilder.setCallback(outputCallback);
        streamBuilder.setErrorCallback(audioStreamErrorCallback);

        oboe::Result result = streamBuilder.openStream(&audioStream);
        if (result != oboe::Result::OK) {
            fprintf(stderr, "Failed to init audio stream");
            delete outputCallback;
            delete audioStreamErrorCallback;
            outputCallback = NULL;
            audioStreamErrorCallback = NULL;
        } else {
            Frontend::Init_Audio(audioStream->getSampleRate());
        }
    }

    void cleanupAudioOutputStream()
    {
        if (audioStream != NULL) {
            if (audioStream->getState() < oboe::StreamState::Closing) {
                audioStream->requestStop();
                audioStream->close();
            }
            delete audioStream;
            delete outputCallback;
            delete audioStreamErrorCallback;
            audioStream = NULL;
            outputCallback = NULL;
            audioStreamErrorCallback = NULL;
        }
    }

    void setupMicInputStream()
    {
        micInputCallback = new MicInputOboeCallback(MIC_BUFFER_SIZE);
        oboe::AudioStreamBuilder micStreamBuilder;
        micStreamBuilder.setChannelCount(1);
        micStreamBuilder.setFramesPerCallback(1024);
        micStreamBuilder.setSampleRate(44100);
        micStreamBuilder.setFormat(oboe::AudioFormat::I16);
        micStreamBuilder.setDirection(oboe::Direction::Input);
        micStreamBuilder.setInputPreset(oboe::InputPreset::Generic);
        micStreamBuilder.setPerformanceMode(oboe::PerformanceMode::PowerSaving);
        micStreamBuilder.setSharingMode(oboe::SharingMode::Exclusive);
        micStreamBuilder.setCallback(micInputCallback);

        oboe::Result micResult = micStreamBuilder.openStream(&micInputStream);
        if (micResult != oboe::Result::OK) {
            micInputType = 1;
            fprintf(stderr, "Failed to init mic audio stream");
            delete micInputCallback;
            micInputCallback = NULL;
        } else {
            Frontend::Mic_SetExternalBuffer(micInputCallback->buffer, MIC_BUFFER_SIZE);
        }
    }

    void resetAudioOutputStream()
    {
        cleanupAudioOutputStream();
        setupAudioOutputStream();
        if (audioStream != NULL) {
            audioStream->requestStart();
        }
    }

    void copyString(char** dest, const char* source)
    {
        if (source == NULL) {
            *dest = NULL;
            return;
        }

        int length = strlen(source);
        *dest = (char*) malloc(length + 1);
        strcpy(*dest, source);
    }
}

