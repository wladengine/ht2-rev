# Dot-source this file; no machine-specific configuration is committed.
function Resolve-WindowsToolchain {
    param([string]$VcDir, [string]$SdkRoot, [string]$SdkVersion = '10.0.26100.0', [string]$Python = 'python')
    $ErrorActionPreference = 'Stop'
    if ($env:HT2_SDK_VERSION) { $SdkVersion = $env:HT2_SDK_VERSION }
    if ($env:HT2_PYTHON) { $Python = $env:HT2_PYTHON }
    if (!$VcDir) {
        $candidates = @($env:VCToolsInstallDir)
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $vswhere) {
            $installs = & $vswhere -all -products '*' -property installationPath
            if ($LASTEXITCODE) { throw 'vswhere failed' }
            foreach ($install in $installs) {
                $candidates += "$install\VC\Tools\MSVC\14.29.30133"
            }
        }
        $VcDir = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath "$_\bin\HostX86\x86\cl.exe") -and $_.TrimEnd('\').EndsWith('14.29.30133') } | Select-Object -First 1
        if (!$VcDir) { throw 'MSVC v142 (14.29) is required. Install the v142 x86/x64 component or pass -VcDir. No automatic v143 fallback.' }
    }
    $VcDir = (Resolve-Path -LiteralPath $VcDir).Path.TrimEnd('\')
    $compiler = "$VcDir\bin\HostX86\x86\cl.exe"
    $version = (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion
    if ($version -notlike '19.29.*') { throw "Expected MSVC 19.29 x86, found $version at $compiler" }
    if (!$SdkRoot) {
        $SdkRoot = $env:WindowsSdkDir
        if (!$SdkRoot) { $SdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10" }
    }
    $SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path.TrimEnd('\')
    $SdkVersion = $SdkVersion.TrimEnd('\')
    foreach ($path in @("$VcDir\bin\HostX86\x86\nmake.exe", "$VcDir\bin\HostX86\x86\link.exe", "$VcDir\bin\HostX86\x86\lib.exe", "$VcDir\include\vcruntime.h", "$VcDir\lib\x86\libcmt.lib", "$VcDir\lib\x86\legacy_stdio_definitions.lib", "$SdkRoot\Include\$SdkVersion\um\windows.h", "$SdkRoot\Lib\$SdkVersion\ucrt\x86\libucrt.lib", "$SdkRoot\Lib\$SdkVersion\um\x86\ddraw.lib", "$SdkRoot\Lib\$SdkVersion\um\x86\dsound.lib")) {
        if (!(Test-Path -LiteralPath $path)) { throw "Missing build prerequisite: $path" }
    }
    $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $pythonInfo = & $Python -c 'import sys, pefile; print(sys.version.split()[0] + " / pefile " + pefile.__version__)'
    if ($LASTEXITCODE) { throw 'Python with pefile is required (python -m pip install pefile==2024.8.26)' }
    $hashes=[ordered]@{}
    foreach ($path in @($compiler, "$VcDir\bin\HostX86\x86\c1xx.dll", "$VcDir\bin\HostX86\x86\c2.dll", "$VcDir\bin\HostX86\x86\link.exe", "$VcDir\bin\HostX86\x86\lib.exe", "$VcDir\lib\x86\libcmt.lib", "$VcDir\lib\x86\libvcruntime.lib", "$VcDir\lib\x86\legacy_stdio_definitions.lib", "$SdkRoot\Lib\$SdkVersion\ucrt\x86\libucrt.lib")) {
        $hashes[$path]=(Get-FileHash -LiteralPath $path).Hash
    }
    [pscustomobject]@{
        VcDir=$VcDir; SdkRoot=$SdkRoot; SdkVersion=$SdkVersion; Python=$Python
        CompilerVersion=$version; PythonVersion=$pythonInfo
        Hashes=$hashes
        NMake="$VcDir\bin\HostX86\x86\nmake.exe"
        MakeArgs=@("VCDIR=$VcDir", "SDKROOT=$SdkRoot", "SDKVER=$SdkVersion", "PYTHON=`"$Python`"")
    }
}
