#ifndef MELONDS_MELONDS_H
#define MELONDS_MELONDS_H

#include "../types.h"

namespace MelonDSAndroid {
    extern char* configDir;

    extern void setup(char* configDirPath);

    /**
     * Loadas the NDS ROM and, optionally, the GBA ROM.
     *
     * @param romPath The path to the NDS rom
     * @param sramPath The path to the rom's SRAM file
     * @param loadDirect If the game should be booted directly
     * @param loadGbaRom If a GBA ROM must also be loaded
     * @param gbaRom The path to the GBA ROM
     * @param gbaSram The path to the GBA rom's SRAM file
     * @return The load result. 0 if everything was loaded successfully, 1 if the NDS ROM was loaded but the GBA ROM
     * failed to load, 2 if the NDS ROM failed to load
     */
    extern int loadRom(char* romPath, char* sramPath, bool loadDirect, bool loadGbaRom, char* gbaRom, char* gbaSram);
    extern void start(u64 initialTicks);
    extern void loop(u64 currentTicks);
    extern void pause();
    extern void resume();
    extern void copyFrameBuffer(void* dstBuffer);
    extern int getFPS();
    extern float getTargetFPS();
    extern void cleanup();
}

#endif //MELONDS_MELONDS_H
