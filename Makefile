# Invoke through tools/build-windows.ps1 from the private build directory.
ROOT = ..\..

# 32-bit compiler and linker
!IFNDEF VCDIR
VCDIR = $(VCToolsInstallDir)
!ENDIF
CC = "$(VCDIR)\bin\HostX86\x86\cl.exe"
LINK = "$(VCDIR)\bin\HostX86\x86\link.exe"
!IFNDEF SDKROOT
SDKROOT = $(WindowsSdkDir)
!ENDIF
!IFNDEF SDKVER
SDKVER = $(WindowsSDKVersion)
!ENDIF
SDKINC = $(SDKROOT)\Include\$(SDKVER)
# tools/build-windows.ps1 discovers and validates these paths.
PYTHON = python

RES = king.res
OUT = king.exe

ALLCPPOBJS = data.obj x87.obj data_image.obj hitch_solver.obj copter_step.obj ai_statics.obj ai_containers.obj ai_drivers.obj ai_saveload.obj ai_task.obj ai_traffic.obj ai_world.obj vector_copy.obj rope_balance.obj cargo_orders.obj road_graph.obj route_search.obj clusters.obj controls.obj core.obj directdraw.obj framework.obj game_app.obj input.obj main_loop.obj mfc.obj media.obj renderer.obj scene.obj sound_files.obj spans.obj vecmath.obj video_mode.obj marine_core.obj marine_grid.obj marine_startup.obj mesh_construct.obj refinement_math.obj wave_vertices.obj deque_pages.obj evaluator.obj native_values.obj natives.obj runtime.obj values.obj directsound.obj provider_records.obj sound_gate.obj timer.obj grid_base.obj grid_support.obj grid_topology.obj terrain.obj terrain_grid.obj gzip.obj drivetrain.obj effects.obj force_feedback.obj game_shell.obj helicopter.obj king_app.obj moving_bodies.obj moving_items.obj object_ids.obj scenery.obj startup.obj vehicle.obj vehicle_engine.obj vehicle_physics.obj vehicle_setup.obj weather.obj world.obj world_objects.obj
CPPOBJS = $(ALLCPPOBJS) crt.obj

# The C++ code names its libraries itself (the modern static C runtime):
# /Zl no default libraries, /GS- no security cookies, /GR- /EHs-c- no RTTI, no EH.
# /arch:IA32: floating point on the x87 FPU like the assembly (see $(ROOT)\src\x87.h)
CFLAGS = /nologo /c /O2 /W3 /GS- /Zl /GR- /EHs-c- /arch:IA32 /I"$(ROOT)\src\common" /I"$(ROOT)\src\ai" /I"$(ROOT)\src\truck" /I"$(VCDIR)\include" /I"$(SDKINC)\um" /I"$(SDKINC)\shared" /I"$(SDKINC)\ucrt"

LIBS = ini-decimal.lib WSOCK32.lib winmm.lib dinput-import.lib DDRAW.lib MSVFW32.lib DSOUND.lib KERNEL32.lib USER32.lib GDI32.lib WINSPOOL.lib ADVAPI32.lib SHELL32.lib COMCTL32.lib ole32.lib OLEAUT32.lib
# the modern static C runtime; legacy_stdio_definitions has
# the printf family as functions
CRTLIBS = libcmt.lib libvcruntime.lib libucrt.lib legacy_stdio_definitions.lib
LIBPATH_FLAGS = /LIBPATH:"$(VCDIR)\lib\x86" /LIBPATH:"$(SDKROOT)\Lib\$(SDKVER)\ucrt\x86" /LIBPATH:"$(SDKROOT)\Lib\$(SDKVER)\um\x86" /ALTERNATENAME:_DirectInputCreateA@16=_DirectInputCreateA

build: resources $(OUT)

# Only the import thunk is generated; implementation remains Windows DINPUT.dll.
# The alias preserves the caller's stdcall ABI and imports the undecorated export.
dinput-import.lib: $(ROOT)\tools\dinput.def
	"$(VCDIR)\bin\HostX86\x86\lib.exe" /nologo /machine:x86 /def:$(ROOT)\tools\dinput.def /out:$@

# Only these numeric conversions use the historical Windows CRT.
ini-decimal.lib: $(ROOT)\tools\ini-decimal.def
	"$(VCDIR)\bin\HostX86\x86\lib.exe" /nologo /machine:x86 /def:$(ROOT)\tools\ini-decimal.def /out:$@

{$(ROOT)\src\ai}.cpp.obj:
	$(CC) $(CFLAGS) /Fo$@ $<

{$(ROOT)\src\engine}.cpp.obj:
	$(CC) $(CFLAGS) /Fo$@ $<

{$(ROOT)\src\marine}.cpp.obj:
	$(CC) $(CFLAGS) /Fo$@ $<

{$(ROOT)\src\script}.cpp.obj:
	$(CC) $(CFLAGS) /Fo$@ $<

{$(ROOT)\src\sound}.cpp.obj:
	$(CC) $(CFLAGS) /Fo$@ $<

{$(ROOT)\src\terrain}.cpp.obj:
	$(CC) $(CFLAGS) /Fo$@ $<

{$(ROOT)\src\truck}.cpp.obj:
	$(CC) $(CFLAGS) /Fo$@ $<

# These copy and rope operations must unwind across C-linkage allocation calls.
provider_records.obj: $(ROOT)\src\sound\provider_records.cpp
	$(CC) $(CFLAGS) /EHs /EHc- /Fo$@ $(ROOT)\src\sound\provider_records.cpp

vector_copy.obj: $(ROOT)\src\ai\vector_copy.cpp
	$(CC) $(CFLAGS) /EHs /EHc- /Fo$@ $(ROOT)\src\ai\vector_copy.cpp

rope_balance.obj: $(ROOT)\src\ai\rope_balance.cpp
	$(CC) $(CFLAGS) /EHs /EHc- /Fo$@ $(ROOT)\src\ai\rope_balance.cpp

# Deque page allocation releases completed pages when the retry handler throws.
deque_pages.obj: $(ROOT)\src\script\deque_pages.cpp
	$(CC) $(CFLAGS) /EHs /EHc- /Fo$@ $(ROOT)\src\script\deque_pages.cpp

# /fp:fast: the x87 helpers become the FPU instructions themselves (x87.cpp)
x87.obj: $(ROOT)\src\common\x87.cpp
	$(CC) $(CFLAGS) /fp:fast /Fo$@ $(ROOT)\src\common\x87.cpp

$(CPPOBJS): $(ROOT)\Makefile $(ROOT)\src\ai\ai.h $(ROOT)\src\ai\road.h $(ROOT)\src\common\king.h $(ROOT)\src\common\vecmath.h $(ROOT)\src\common\x87.h $(ROOT)\src\truck\car.h
terrain.obj helicopter.obj: $(ROOT)\src\common\terrain_view.h
scene.obj framework.obj: $(ROOT)\src\engine\outline_sphere.h

# Resources (icon, cursor, title bitmaps, strings) taken from the game's king.exe
resources:
!IF "$(GAME_EXE)" == ""
	@echo king.res needs the game's original king.exe: nmake "GAME_EXE=C:\path\to\king.exe"
	@exit 1
!ENDIF
	$(PYTHON) $(ROOT)\extractResources.py "$(GAME_EXE)" $(RES)

$(RES): $(ROOT)\extractResources.py
	$(PYTHON) $(ROOT)\extractResources.py "$(GAME_EXE)" $(RES)

# /NXCOMPAT:NO /TSAWARE:NO keep the original's DLL characteristics (no DEP)
$(OUT): $(CPPOBJS) $(RES) dinput-import.lib ini-decimal.lib
	$(LINK) /nologo $(CPPOBJS) $(RES) /SUBSYSTEM:WINDOWS,5.01 /MACHINE:X86 $(LIBPATH_FLAGS) $(LIBS) $(CRTLIBS) /FIXED /ENTRY:WinMainCRTStartup /RELEASE /INCREMENTAL:NO /DEBUG:NONE /NXCOMPAT:NO /TSAWARE:NO /OUT:$(OUT) /MAP:king.map

# king.res stays: rebuilding it needs the game's king.exe
clean:
	-del /q $(ALLCPPOBJS) crt.obj sound-source.obj sound-source.exe deferred-sound.bin $(OUT) king.map dinput-import.lib dinput-import.exp ini-decimal.lib ini-decimal.exp 2>nul

distclean: clean
	-del /q king.res king.res.json 2>nul

sound-source.obj: $(ROOT)\tests\sound_source.cpp
	$(CC) $(CFLAGS) /D_CRT_SECURE_NO_WARNINGS /Fosound-source.obj $(ROOT)\tests\sound_source.cpp

sound-source.exe: sound-source.obj $(CPPOBJS) dinput-import.lib ini-decimal.lib
	$(LINK) /nologo sound-source.obj $(CPPOBJS) /SUBSYSTEM:CONSOLE /MACHINE:X86 $(LIBPATH_FLAGS) $(LIBS) $(CRTLIBS) /ENTRY:mainCRTStartup /BASE:0x20000000 /FIXED:NO /DYNAMICBASE:NO /OUT:$@

check: build sound-source.exe
	.\sound-source.exe deferred-sound.bin
