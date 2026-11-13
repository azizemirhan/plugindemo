# Loading a validation build in FL Studio without installing it

**Diagnosis: the artefact is fine and FL Studio never saw it. A VST3 is not
discoverable through a custom search path at all — FL Studio's "Plugin search
paths" are for VST1/VST2, and a VST3 has to be found in the standard Windows
VST3 location.** The artefact passes every check: hash, architecture,
`LoadLibrary`, `GetPluginFactory`, dependencies, identity.

`Scripts\prepare-fl-validation.ps1` puts a **directory junction** in the system
VST3 folder pointing at the build tree. The plugin is not copied, so the host
loads the validation artefact itself.

### Three attempts, and why the first two could not work

| | what it was | why it failed |
|---|---|---|
| 1 | custom search paths into the build tree | custom paths are VST2-only. Both were also at the wrong level — one a path that did not exist, one the bundle instead of its parent — but the level was never the deciding problem |
| 2 | junction in `%LOCALAPPDATA%\Programs\Common\VST3` | a standard VST3 location by the specification, and some hosts scan it; this FL Studio did not find the plugin there |
| 3 | **junction in `%CommonProgramFiles%\VST3`** | the system location. This is the one |

Attempt 1's diagnosis is kept below because it is still the explanation for the
*first* symptom, and because the malformed entries had to be cleaned up either
way.

---

## 1. The artefact is not the problem

```
C:\JuceBuilds\av-phase7\AuroraVerb_artefacts\RelWithDebInfo\VST3\AuroraVerb.vst3
```

| check | result |
|---|---|
| bundle exists | yes |
| `Contents\x86_64-win\AuroraVerb.vst3` | yes, 15 324 672 bytes |
| SHA-256 | `2C52D78F…5BA6151` — **matches** |
| PE signature | `PE\0\0`, valid |
| machine | `0x8664` — x86-64 |
| optional header | `PE32+` — 64-bit |
| `LoadLibrary` | **succeeds** |
| `GetPluginFactory` | present |
| `InitDll` / `ExitDll` | present |
| `bundleEntry` / `bundleExit` | absent — correct, those are macOS/Linux |
| MSVC runtime imports | **none** — static CRT, no redistributable needed |
| imported DLLs | only system: `kernel32`, `user32`, `ole32`, `oleaut32`, `shell32`, `comdlg32`, `uxtheme`, `dwmapi`, `ws2_32`, `api-ms-win-shcore-scaling` |
| identity | `AuroraVerb`, category `Fx|Reverb`, classes `av::AuroraVerbProcessor` / `av::AuroraVerbEditor` |

The module loads in-process and exposes the Windows VST3 entry point. There is
no missing dependency, no architecture mismatch, no Smart App Control block and
no class-ID problem.

`pluginval` is **not installed on this machine** — neither
`C:\JuceBuilds\pluginval\pluginval.exe` nor `Tools\pluginval\pluginval.exe`
exists — so `Scripts\run-pluginval.ps1` could not be used. The `LoadLibrary` +
`GetPluginFactory` probe above answers the same question a host asks when it
scans, which is what mattered here.

### Bundle metadata

The bundle contains exactly one thing: `Contents\x86_64-win\AuroraVerb.vst3`.
There is no `moduleinfo.json`, and that is **deliberate and documented** in
`CMakeLists.txt`: JUCE generates it by running a helper it has just built,
Windows Smart App Control blocks locally-built unsigned executables, and the
step fails the build intermittently. `moduleinfo.json` only speeds host
scanning; a bundle without it is valid, and the comment in `CMakeLists.txt`
records that the plugin passes pluginval at strictness 10 either way.

### Which build this is

`CMakeCache.txt` for `C:\JuceBuilds\av-phase7`:

```
AV_DEV_DIAGNOSTICS         ON
AV_DEV_AUDIO_CAPTURE       OFF
AV_DEV_ALLPASS_CANDIDATE   1
AV_COPY_PLUGIN_AFTER_BUILD OFF
```

That is exactly the `validation` profile from
`Docs/ShippingBuildHardening.md` §3. So the artefact is the right kind of
build — but it has telemetry compiled in, and the development configuration
opens a diagnostic session folder **per instantiation** under
`%USERPROFILE%\AuroraVerbDiagnostics`. That is backlog item 2 in the hardening
doc, not a fault, and it is worth knowing before a listening session leaves
dozens of folders behind.

---

## 2. What FL Studio was actually scanning

FL Studio 2026 (`C:\Program Files\Image-Line\FL Studio 2026\FL64.exe`), keys
under `HKCU\Software\Image-Line\FL Studio 26`.

Custom search paths live in the registry, one numbered subkey per path:

```
HKCU\Software\Image-Line\Shared\Plugin search paths\List
```

As found:

| # | custom | path | on disk |
|---|---|---|---|
| 0 | 0 | `%FLPath%\Plugins\Fruity\Effects\` | FL's own token |
| 1 | 0 | `%FLPath%\Plugins\Fruity\Generators\` | FL's own token |
| 2 | 0 | `C:\Program Files\Common Files\VST2\` | exists |
| 3 | 0 | `%FLPath%\Plugins\VST\` | FL's own token |
| 4 | 1 | `…\RelWithDebInfo\VST3\AuroraVerb.vst3\AuroraVerb.vst3\` | **MISSING** |
| 5 | 1 | `…\RelWithDebInfo\VST3\AuroraVerb.vst3\` | exists, but is the bundle |

**Entry 4** has the bundle name doubled. That directory does not exist.

**Entry 5** is the bundle itself. A VST3 host scans a *folder* for `.vst3`
bundles inside it, and the only child of the bundle is `Contents` — which is
not a bundle. So the scan looks inside and finds nothing:

```
inside entry 5            inside the parent folder
  Contents                  AuroraVerb.vst3   <- the bundle
                            AuroraVerb.exp
                            AuroraVerb.lib
                            AuroraVerb.pdb
```

**The parent folder was never in the list.** That is the answer to "A or B":
**A**, the parent, is the correct level. B cannot work at any version of any
host.

A further sign the key was edited outside FL: `Count` read **4** while **six**
subkeys existed, so the two added entries were very likely not being read at
all. Both were wrong anyway, so this is secondary — but the script corrects
`Count` as part of the rewrite.

### The cache was not the problem

| evidence | reading |
|---|---|
| `Presets\Plugin database\Installed\Effects` | empty |
| 281 files in the plugin database | **none** mentions `Aurora` or `JuceBuilds` |
| `FL Plugin Scanner` → `verify plugins` | `1` — failures would have been reported |
| `FL Plugin Scanner` → `rescan failed` | `0` |

FL has no record of AuroraVerb at all — not a failure, not a blacklist entry,
nothing. It is not filtering the plugin; it has never been pointed at a folder
containing it. So no database surgery is needed and none should be done.

---

## 3. The previous working method, recovered

From `README.md`:

> The VST3 is installed after each build into `%LOCALAPPDATA%\Programs\Common\VST3`
> — the per-user VST3 folder, so builds never need an elevated shell.

That was `AV_COPY_PLUGIN_AFTER_BUILD`, whose CMake default is `ON`, copying to
`AV_VST3_COPY_DIR`. It is the per-user standard VST3 folder, **not** the
machine-wide production folder — so it was never a "global install".

It stopped happening on purpose. `Docs/ShippingBuildHardening.md` §8 records
that `build.ps1` was auto-installing every build into a folder every DAW scans,
whatever profile it had been built with, and closed it:

> `Scripts/build.ps1` now passes `-DAV_COPY_PLUGIN_AFTER_BUILD=OFF` on every
> configure.

So the old convenience was removed as a defect, and nothing replaced it. **That
is the gap this document closes**: a deliberate, reversible, verified way to
reach one artefact, instead of a side effect of every build.

### State on this machine

All four locations the hardening doc describes are **absent**:

```
MISSING  C:\Program Files\Common Files\VST3
MISSING  %LOCALAPPDATA%\Programs\Common\VST3
MISSING  C:\Program Files (x86)\Common Files\VST3
MISSING  %LOCALAPPDATA%\Programs\Common\VST3-AuroraVerb-backups
```

So the installed production artefact `2B451890…02BD8` and the rollback archive
`A49D477D…622464` are **not on this machine**. The invariant "do not overwrite
the installed production artefact" is satisfied by there being nothing to
overwrite — not by a check that passed. Anyone relying on that rollback archive
should know it is not here.

---

## 4. The fix

```powershell
# FL Studio must be closed.
.\Scripts\prepare-fl-validation.ps1
```

An ordinary (non-elevated) PowerShell is fine. The script elevates **one
command** — the `mkdir` + `mklink` pair — and Windows raises a single UAC
prompt for it. Everything else, including every hash and every check, runs as
the ordinary user. Starting the shell as Administrator is also fine and then
no prompt appears.

| | |
|---|---|
| junction | `C:\Program Files\Common Files\VST3\AuroraVerb.vst3` |
| target | `C:\JuceBuilds\av-phase7\AuroraVerb_artefacts\RelWithDebInfo\VST3\AuroraVerb.vst3` |
| expected SHA-256 | `2C52D78F701A3024167A5651A6A90D5F5ADB971B147B30871631365715BA6151` |

What it does, in order:

1. **Refuses while FL Studio is running.** A plugin folder must not change
   under a running host, and FL also rewrites its search-path registry key
   from memory on exit, so an edit made while it is open is silently
   discarded — which looks exactly like the edit not working.
2. **Verifies the source** bundle, its Windows binary and its SHA-256, and
   stops on a mismatch.
3. **Inspects the destination.** If `AuroraVerb.vst3` is already there as a
   *real directory*, it prints that directory's hash and stops: a real
   directory is an installed plugin, and this script is not an installer. If
   it is a junction pointing outside an `AuroraVerb_artefacts` tree, it also
   stops. It only replaces a junction it can see is one of its own.
4. **Asks whether elevation is actually needed**, by probing the folder for
   writability rather than assuming, so no UAC prompt is raised when none is
   required.
5. **Creates the junction**, making the parent `VST3` folder first if it does
   not exist. On this machine it did not.
6. **Verifies through the junction**: resolves the link target, hashes the
   binary *through the link*, and fails unless it equals the source hash. The
   bytes the host will load are therefore the bytes that were verified.
7. **Removes the superseded per-user junction**, so there is only one
   validation path to be confused by later. `-KeepUserJunction` keeps it.
8. **Removes the AuroraVerb custom search-path entries**, which provably
   cannot find a VST3. An entry that cannot work is worse than no entry: it
   looks like provision. Unrelated paths are carried across untouched and the
   key is exported to `%LOCALAPPDATA%\AuroraVerb-fl-backups\` first.
   `-KeepScanPaths` leaves the registry alone.

### Then, in FL Studio

1. **Options → Manage plugins**
2. Leave **Verify plugins** ON, so a plugin that fails to load is reported
   instead of skipped silently.
3. Press **Find more plugins**.
4. AuroraVerb appears under **Effects**, maker **IndieDSP**.

A full or forced rescan is not needed, and **Rescan previously failed plugins**
is not needed either: FL has no cached AuroraVerb entry of any kind — its
plugin database holds 281 files and not one mentions `Aurora` or `JuceBuilds`.
There is nothing cached to invalidate. Do not wipe the plugin database.

### Undo

```powershell
.\Scripts\prepare-fl-validation.ps1 -Remove
```

Removes the junction with `rmdir`, which deletes the link and not its target.
It **never removes the parent `VST3` folder** — if the folder was created by
this script and is now empty, it is left for you to delete by hand.

### How it was tested

The privileged step cannot be rehearsed against Program Files without actually
writing there, so the whole script was run end to end against a scratch
directory and a byte-for-byte copy of FL's registry key. All three paths were
exercised and the copies deleted afterwards:

| path | result |
|---|---|
| create | junction made, resolved, hashed through the link — `2C52D78F…5BA6151`, matches |
| `-Remove` | link deleted, **source bundle intact**, parent folder left in place |
| refusal | a planted real directory was **not** overwritten, its hash was printed, the script stopped |

The `mklink` / `rmdir` command forms were validated separately before being
wired in.

---

## 5. What was deliberately not done

- **No copy.** The junction means the host loads the validation artefact
  itself, so the bytes under test cannot drift from the bytes that were
  verified.
- **No plugin-database wipe.** FL has no AuroraVerb record to clear.
- **No rebuild.** The artefact verified byte-for-byte against the expected
  hash.
- **No change to unrelated search paths.** The stock entries are carried
  across untouched; the script's filter is `*AuroraVerb_artefacts*`.
- **No `AV_COPY_PLUGIN_AFTER_BUILD=ON`.** Re-enabling it would restore the
  defect the hardening pass closed: every build installing itself as a side
  effect, whatever profile it is. It stays OFF.
- **No change to `Source/`, the DSP, the defaults, the CMake configuration or
  any Phase 7 result.** This is host plumbing and nothing else.
- **Nothing written to Program Files except the one junction** (and the `VST3`
  folder itself, which did not exist). No binary is installed there.
