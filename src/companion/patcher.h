// Creates OREGONM.EXE (the hooked copy of the game) from OREGON.EXE.
// The patch list must match tools/patch_exe.py and src/dos/otmod.asm.
#pragma once
#include <string>

struct PatchResult {
    bool ok = false;
    bool written = false;      // a new OREGONM.EXE was written
    std::string message;
};

// gameDir: folder holding OREGON.EXE.  Rewrites OREGONM.EXE only when it is
// missing or differs from a freshly patched copy.
PatchResult ensurePatchedExe(const std::string& gameDir);
