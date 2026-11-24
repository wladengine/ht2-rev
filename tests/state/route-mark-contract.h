// Steam 8.1 0x483ff0. Force-include in the definition and the caller with
// MSVC /Zs to check the C++ contract even when the linker emits the same jump.
extern "C" void __cdecl Route_Call529880(int player, int road);
