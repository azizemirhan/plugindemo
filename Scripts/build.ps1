<#
    AuroraVerb build script (Windows / MSVC).

    Usage:
        .\Scripts\build.ps1                  # Release build of the VST3 + Standalone
        .\Scripts\build.ps1 -Config Debug
        .\Scripts\build.ps1 -Tests           # also build and run the DSP tests
        .\Scripts\build.ps1 -Clean

    Note on paths: JUCE's build helper (juceaide) crashes when the *build*
    directory contains non-ASCII characters. If this project lives under such a
    path, the script relocates the build tree to an ASCII fallback
    automatically. The source tree itself can stay where it is.
#>

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string] $Config = 'Release',

    [string] $BuildDir,
    [string] $JucePath,

    # What the artefact is for. Decides diagnostics and capture, and is passed
    # explicitly to CMake so an existing cache cannot decide it instead.
    [ValidateSet('production', 'development', 'validation')]
    [string] $Profile = 'production',

    # Development override for PHASE 5B A/B work. 1 is the shipped architecture.
    [ValidateSet('0', '1', '2', '3')]
    [string] $AllpassCandidate = '1',

    [switch] $Tests,
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

# --- Locate cmake ----------------------------------------------------------

$cmakeExe = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmakeExe) {
    $fallback = 'C:\Program Files\CMake\bin\cmake.exe'
    if (Test-Path $fallback) { $cmakeExe = $fallback }
}
if (-not $cmakeExe) {
    throw "cmake was not found. See README.md -> 'Toolchain'."
}

# --- Choose a build directory juceaide can cope with -----------------------

if (-not $BuildDir) {
    if ($root -match '[^\x00-\x7F]') {
        $BuildDir = Join-Path $env:SystemDrive ('JuceBuilds\' + (Split-Path -Leaf $root))
        Write-Warning "Project path contains non-ASCII characters, which crashes juceaide."
        Write-Warning "Building in '$BuildDir' instead. Pass -BuildDir to override."
    } else {
        $BuildDir = Join-Path $root 'build'
    }
}

# --- JUCE ------------------------------------------------------------------

if (-not $JucePath) {
    $local = Join-Path $root 'External\JUCE'
    if (Test-Path (Join-Path $local 'CMakeLists.txt')) { $JucePath = $local }
}

# --- Clean -----------------------------------------------------------------

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Removing $BuildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# --- Configure -------------------------------------------------------------

$configureArgs = @(
    '-S', $root,
    '-B', $BuildDir,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64'
)

if ($JucePath) { $configureArgs += "-DAV_JUCE_PATH=$($JucePath -replace '\\', '/')" }

# Every flag that changes what the plugin *is* is passed explicitly, on every
# configure, whether or not it differs from the CMake default.
#
# It used to be that none of them were, and the result was decided by whatever
# an existing CMakeCache happened to hold. That is how every artefact validated
# in a host came to be built with the reverb engine enabled by a development
# flag, while the default configuration produced a plugin with no reverb in it —
# and nothing anywhere said which one you had just built.
#
# -Profile picks the intent; the flags are never inherited from the cache.
$profiles = @{
    'production'  = @{ Diagnostics = 'OFF'; Capture = 'OFF' }
    'development' = @{ Diagnostics = 'ON';  Capture = 'ON'  }
    'validation'  = @{ Diagnostics = 'ON';  Capture = 'OFF' }
}

if (-not $profiles.ContainsKey($Profile)) {
    throw "Unknown -Profile '$Profile'. Use one of: $($profiles.Keys -join ', ')."
}

$configureArgs += "-DAV_DEV_DIAGNOSTICS=$($profiles[$Profile].Diagnostics)"
$configureArgs += "-DAV_DEV_AUDIO_CAPTURE=$($profiles[$Profile].Capture)"
$configureArgs += "-DAV_DEV_ALLPASS_CANDIDATE=$AllpassCandidate"
$configureArgs += "-DAV_BUILD_TESTS=$(if ($Tests) { 'ON' } else { 'OFF' })"

# No build installs anything. This was the one identity-deciding flag the
# hardening pass left to the CMake default, which is ON - so every build
# through this script quietly copied its artefact into the per-user VST3 scan
# folder, whatever profile it had just been built with. Installing is a
# separate, gated, deliberate act: Scripts\install-production-vst3.ps1.
$configureArgs += "-DAV_COPY_PLUGIN_AFTER_BUILD=OFF"

if ($AllpassCandidate -ne '1') {
    Write-Warning "AV_DEV_ALLPASS_CANDIDATE=$AllpassCandidate is a comparison build, not the shipped architecture. Do not install it."
}

Write-Host "Profile: $Profile  (diagnostics $($profiles[$Profile].Diagnostics), capture $($profiles[$Profile].Capture), candidate $AllpassCandidate)" -ForegroundColor Cyan

Write-Host "Configuring -> $BuildDir" -ForegroundColor Cyan
& $cmakeExe @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

# --- Build -----------------------------------------------------------------

Write-Host "Building ($Config)..." -ForegroundColor Cyan
& $cmakeExe --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

# --- Tests -----------------------------------------------------------------

if ($Tests) {
    Write-Host "`nRunning DSP tests..." -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'run-tests.ps1') -BuildDir $BuildDir -Config $Config
    if ($LASTEXITCODE -ne 0) { throw "DSP tests failed." }
}

# --- Report ----------------------------------------------------------------

$vst3    = Join-Path $BuildDir "AuroraVerb_artefacts\$Config\VST3\AuroraVerb.vst3"
$standalone = Join-Path $BuildDir "AuroraVerb_artefacts\$Config\Standalone\AuroraVerb.exe"
$installed  = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\AuroraVerb.vst3'

Write-Host "`nDone." -ForegroundColor Green
Write-Host "  VST3:       $vst3"
Write-Host "  Standalone: $standalone"

# Nothing was installed, so anything sitting in a scan folder is a leftover
# from before this script stopped copying. Say so rather than printing it under
# a heading that reads like a report of what this build just did.
if (Test-Path $installed)
{
    Write-Warning "A plugin is present at $installed."
    Write-Warning "This build did not put it there. Remove it unless you meant to keep it."
}
