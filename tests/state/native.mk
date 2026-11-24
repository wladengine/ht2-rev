# Included by the state regression runner after building the production objects.
# CHECK_DIR must exist. CHECK_SOURCE supplies an ordinary x86 console main.
!include ..\..\Makefile

native-check: $(CPPOBJS) dinput-import.lib ini-decimal.lib
	$(CC) $(CFLAGS) /Fo"$(CHECK_DIR)\case.obj" "$(CHECK_SOURCE)"
	$(LINK) /nologo "$(CHECK_DIR)\case.obj" $(CPPOBJS) /SUBSYSTEM:CONSOLE /MACHINE:X86 $(LIBPATH_FLAGS) $(LIBS) $(CRTLIBS) /FIXED /ENTRY:mainCRTStartup /OUT:"$(CHECK_DIR)\case.exe"
	"$(CHECK_DIR)\case.exe"

route-contract:
	$(CC) $(CFLAGS) /Zs /FI"$(ROOT)\tests\state\route-mark-contract.h" $(ROOT)\src\ai\ai_traffic.cpp $(ROOT)\src\ai\ai_world.cpp
