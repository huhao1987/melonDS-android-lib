#include <oboe/Oboe.h>
#include "MelonDS.h"
#include "../NDS.h"
#include "../GPU.h"
#include "../GPU3D.h"
#include "../SPU.h"
#include "../Platform.h"
#include "OboeCallback.h"

u32* frameBuffer;

u64 startTick = 0;
u64 lastTick = startTick;
u64 lastMeasureTick = lastTick;
u32 currentFps = 0;
oboe::AudioStream *audioStream;

namespace MelonDSAndroid
{
    char* configDir;

    void setup(char* configDirPath)
    {
        configDir = configDirPath;

        frameBuffer = new u32[256 * 384 * 4];

        // TODO: Gotta find the correct sound setup
        oboe::AudioStreamBuilder streamBuilder;
        streamBuilder.setChannelCount(2);
        streamBuilder.setFramesPerCallback(1024);
        streamBuilder.setFormat(oboe::AudioFormat::I16);
        streamBuilder.setDirection(oboe::Direction::Output);
        streamBuilder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        streamBuilder.setSharingMode(oboe::SharingMode::Exclusive);
        streamBuilder.setCallback(new OboeCallback());

        oboe::Result result = streamBuilder.openStream(&audioStream);
        if (result != oboe::Result::OK)
        {
            fprintf(stderr, "Failed to init audio stream");
        }

        NDS::Init();
        GPU3D::InitRenderer(false);
    }

    int loadRom(char* romPath, char* sramPath, bool loadDirect, bool loadGbaRom, char* gbaRom, char* gbaSram)
    {
        bool loaded = NDS::LoadROM(romPath, sramPath, loadDirect);
        if (!loaded)
            return 2;

        if (loadGbaRom)
        {
            if (!NDS::LoadGBAROM(gbaRom, gbaSram))
                return 1;
        }

        return 0;
    }

    void start(u64 initialTicks)
    {
        startTick = initialTicks;
        audioStream->requestStart();
        memset(frameBuffer, 0, 256 * 384 * 4);
    }

    void loop(u64 currentTicks)
    {
        NDS::RunFrame();

        int frontbuf = GPU::FrontBuffer;
        if (GPU::Framebuffer[frontbuf][0] && GPU::Framebuffer[frontbuf][1])
        {
            memcpy(frameBuffer, GPU::Framebuffer[frontbuf][0], 256 * 192 * 4);
            memcpy(&frameBuffer[256 * 192], GPU::Framebuffer[frontbuf][1], 256 * 192 * 4);
        }

        u32 delta = currentTicks - lastMeasureTick;
        lastMeasureTick = currentTicks;
        currentFps = 1000 / delta;
    }

    void pause() {
        audioStream->requestPause();
    }

    void resume() {
        audioStream->requestStart();
    }

    void copyFrameBuffer(void* dstBuffer)
    {
        memcpy(dstBuffer, frameBuffer, 256 * 384 * 4);
    }

    int getFPS()
    {
        return currentFps;
    }

    void cleanup()
    {
        NDS::DeInit();
        GPU3D::DeInit();
        audioStream->requestStop();
        audioStream = NULL;

        free(frameBuffer);
        frameBuffer = NULL;
    }
}

void Stop(bool internal)
{
}

bool LocalFileExists(const char* name)
{
    FILE* f = Platform::OpenFile(name, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}