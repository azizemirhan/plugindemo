# Loading a validation build in FL Studio without installing it

**Diagnosis: the artefact is fine and FL Studio never saw it. Both custom
search paths that had been added point at the wrong thing — one at a path that
does not exist, one at the bundle instead of the folder containing it — so
every scan looked in the right build tree at the wrong level and correctly
found nothing.** There was no error to report, which is why none appeared.

Fixed by `Scripts\prepare-fl-validation.ps1`. No install, nothing written to
`C:\Program Files\Common Files\VST3`, no elevated shell.

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
# Close FL Studio first - see below for why.
.\Scripts\prepare-fl-validation.ps1
```

It will:

1. Refuse to run while FL Studio is open.
2. Verify the bundle, binary and **SHA-256**, and stop on a mismatch.
3. Export `HKCU\Software\Image-Line\Shared\Plugin search paths` to
   `%LOCALAPPDATA%\AuroraVerb-fl-backups\plugin-search-paths-<timestamp>.reg`.
4. Remove every entry whose path is inside an `AuroraVerb_artefacts` tree, and
   **leave every other search path exactly as it is**.
5. Add one entry: the bundle's parent folder, `custom=1`, `selected=1`.
6. Renumber the subkeys `0..N-1` and set `Count` to match.
7. Print the list before and after, and confirm the production folder is
   untouched.

Then in FL Studio: **Options → Manage plugins**, confirm the path is listed and
ticked, and press **Find more plugins** with **Verify plugins** left on, so a
plugin that fails to load is reported instead of skipped.

Undo with `-Remove`.

### Why it refuses while FL is running

FL Studio writes this whole key back from memory when it exits. An edit made
while it is open is silently discarded — which looks identical to the edit not
working. The script checks and says so rather than appearing to succeed. FL was
running (`FL64`, pid 14040) when this was diagnosed, which is why the real key
was left alone; the rewrite logic was instead exercised against a byte-for-byte
scratch copy of the key, verified, and the copy deleted.

### If the folder still is not picked up

```powershell
.\Scripts\prepare-fl-validation.ps1 -Junction
```

This additionally creates a **directory junction**:

```
%LOCALAPPDATA%\Programs\Common\VST3\AuroraVerb.vst3
    -> C:\JuceBuilds\av-phase7\AuroraVerb_artefacts\RelWithDebInfo\VST3\AuroraVerb.vst3
```

That is the per-user VST3 folder the README documents — the previous working
method's destination, reached without copying anything. Three properties make
it better than a staging copy:

- **The host loads the validation artefact itself.** The script hashes the
  binary *through the junction* and fails if it does not match, so the thing
  under test cannot drift from the thing that was verified.
- **No elevation.** A junction is not a symlink; `mklink /J` needs no admin.
- **`-Remove` leaves nothing behind.** It removes the link with `rmdir`, which
  deletes the link and not the target, and it refuses to delete a real folder
  it did not create.

It never writes to `C:\Program Files\Common Files\VST3`, and the script asserts
that before creating the link.

---

## 5. What was deliberately not done

- **No global install.** The scan-path method was not proven unworkable — it
  was proven misconfigured, with the exact misconfiguration identified.
- **No plugin-database wipe.** FL has no AuroraVerb record to clear.
- **No rebuild.** The artefact verified byte-for-byte against the expected
  hash.
- **No change to unrelated search paths.** The four stock entries are carried
  across untouched, and the script's filter is `*AuroraVerb_artefacts*`.
- **No `AV_COPY_PLUGIN_AFTER_BUILD=ON`.** Re-enabling it would restore the
  defect the hardening pass closed: every build installing itself as a side
  effect, whatever profile it is.
