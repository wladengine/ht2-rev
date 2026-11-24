// ABI regression against the production Poly_Destroy; no game resources needed.
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern "C" unsigned char KingBss[];
extern "C" void __fastcall Poly_Destroy(BYTE*, void*);
static void* expected;
static void* received;
static int calls;
static ULONG __stdcall Release(void* self) {
    received = self;
    ++calls;
    return 0;
}
// Recover ESP even with the old broken call, so the test reports the ABI error
// deterministically instead of relying on a particular crash after returning.
__declspec(naked) static int __cdecl Invoke(BYTE*) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp+8]
        xor edx, edx
        mov esi, esp
        call Poly_Destroy
        sub esi, esp
        mov eax, esi
        lea esp, [ebp-12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static int Check(const char* name, int enabled, bool object, int expectedCalls) {
    BYTE poly[40] = {};
    void* vtable[3] = {0, 0, (void*)Release};
    void** iface = vtable;
    expected = &iface; received = 0; calls = 0;
    *(int*)(KingBss + 0x720904 - 0x68a820) = enabled;
    *(void**)(poly + 36) = object ? expected : 0;
    BYTE snapshot[40]; memcpy(snapshot, poly, sizeof poly);
    int delta = Invoke(poly);
    int bad = delta || calls != expectedCalls || (calls && received != expected)
        || memcmp(snapshot, poly, sizeof poly);
    printf("%s: esp_delta=%d calls=%d expected_this=%p received_this=%p %s\n",
           name, delta, calls, expected, received, bad ? "FAIL" : "PASS");
    return bad;
}
int main() {
    int bad = Check("null-interface", 1, false, 0);
    bad |= Check("release-disabled", 0, true, 0);
    bad |= Check("stdcall-release", 1, true, 1);
    bad |= Check("repeat-release", 1, true, 1);
    return bad ? 1 : 0;
}
