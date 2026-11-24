// Regression: SoundSource_Update must preserve a valid caller-supplied matrix
// and return normally after its temporary Matrix_RotationZ calls.
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern "C" int __fastcall SoundSource_Update(BYTE*, void*, int, const BYTE*, int, int);
struct GameSound {
    char* name;
    int size;
    void* data;
    int slot;
    FILE* stream;
    int offset;
    unsigned flags;
};
static_assert(sizeof(GameSound) == 28, "Expected x86 sound record");
extern "C" BYTE KingBss[];
extern "C" GameSound* Sound_Create(int name, int slot, int streamed);
extern "C" GameSound* Sound_CreateFromFile(FILE* file, int slot, int size, int streamed);
extern "C" void __fastcall SoundFile_Destroy(GameSound*, void*);

static bool CheckDeferredSounds(const char* path) {
    FILE* file = fopen(path, "wb+");
    if (!file) return false;
    const char payload[] = "sound fixture";
    const int size = sizeof(payload) - 1;
    if (fwrite(payload, 1, size, file) != size || fseek(file, 3, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    int& enabled = *reinterpret_cast<int*>(KingBss + (0x723b28 - 0x68a820));
    GameSound**& registry = *reinterpret_cast<GameSound***>(KingBss + (0x723b34 - 0x68a820));
    const int previousEnabled = enabled;
    GameSound** previousRegistry = registry;
    GameSound* slots[2] = {};
    enabled = 1;
    registry = slots;
    GameSound* packed = Sound_CreateFromFile(file, 0, size - 3, 1);
    GameSound* named = Sound_Create(reinterpret_cast<int>(path), 1, 1);
    bool ok = packed && packed == slots[0] && packed->slot == 0 && packed->size == size - 3 &&
              packed->flags == 4 && packed->stream == file && packed->offset == 3 && !packed->data &&
              named && named == slots[1] && named->slot == 1 && named->size == size &&
              named->flags == 4 && named->name && !strcmp(named->name, path) && !named->data;
    registry = previousRegistry;
    enabled = previousEnabled;
    for (GameSound* sound : slots) {
        if (sound) {
            SoundFile_Destroy(sound, nullptr);
            ::operator delete(sound);
        }
    }
    fclose(file);
    return ok;
}

static int Check(const char* name, int flags, int disabled, int kind, int expected) {
    BYTE source[128] = {};
    float listener[12] = {1,0,0,0,1,0,0,0,1,11,22,33};
    const float original[12] = {1,0,0,0,1,0,0,0,1,11,22,33};
    source[40] = (BYTE)disabled;
    // Exercise volume, direction, point and cone calculations without a live
    // playback handle at +80. This test makes no claim about audible output.
    *(int*)(source + 48) = kind;
    *(float*)(source + 60) = 0.5f;
    *(float*)(source + 64) = 0.2f;
    *(float*)(source + 68) = 2.8f;
    *(float*)(source + 72) = 1.0f;
    *(float*)(source + 76) = 200.0f;
    *(float*)(source + 84) = 10.0f;
    *(float*)(source + 88) = 20.0f;
    *(float*)(source + 92) = 30.0f;
    *(float*)(source + 96) = 1.0f;
    printf("%s: input matrix=%p\n", name, (void*)listener); fflush(stdout);
    int result = SoundSource_Update(source, 0, flags, (const BYTE*)listener, 0, 0);
    if (result != expected || memcmp(listener, original, sizeof listener)) {
        fprintf(stderr, "%s: result=%d expected=%d or listener modified\n", name, result, expected);
        return 1;
    }
    printf("%s: PASS\n", name);
    return 0;
}
int main(int argc, char** argv) {
    if (argc != 2 || !CheckDeferredSounds(argv[1])) {
        fprintf(stderr, "Deferred sound record or registry mismatch\n");
        return 1;
    }
    if (Check("muted", 4, 0, 0, 0)) return 1;
    if (Check("enabled", 0, 0, 0, 1)) return 1;
    if (Check("directional", 0, 0, 1, 1)) return 1;
    if (Check("point", 0, 0, 2, 1)) return 1;
    if (Check("cone", 0, 0, 3, 1)) return 1;
    if (Check("disabled", 0, 1, 0, 0)) return 1;
    puts("PASS: sound-source matrix lifetime and muted/disabled contracts");
    return 0;
}
