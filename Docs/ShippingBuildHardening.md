# Shipping / build hardening — **COMPLETE**

Closed 2026-08-24, after a manual FL Studio host regression on the installed
production artefact. No production DSP was changed at any point in this work:
the diff is confined to `CMakeLists.txt`, `CMakePresets.json`, `Scripts/`, and
the removal of a build flag from `Source/PluginProcessor.{h,cpp}` that never
affected the shipping signal path.

---

## 1. What was actually wrong

Every artefact ever validated in a host had the reverb engine switched on by a
development flag, `AV_DEV_DIFFUSION_PREVIEW`, that was **off by default**. With
it off, the wet path was a copy of the input — a plugin with no reverb in it,
which still built, still loaded, still passed pluginval, and said nothing.

The flag's value did not come from the build script. It came from whatever an
existing `CMakeCache.txt` happened to hold. So *"the default build"* and *"the
build we tested"* were never the same thing, and only the cache knew which one
you had just produced.

A second instance of the same hazard was found in the same audit:
`AV_DEV_ALLPASS_CANDIDATE` defaulted to `0` — the PHASE 5A matrix-only
modulation — while every validated build used `1`, the PHASE 5C shipped
architecture.

Neither was a DSP defect. Both were configuration defects that made the
artefact's identity unknowable after the fact.

## 2. What was changed

| Change | Why |
|---|---|
| `AV_DEV_DIFFUSION_PREVIEW` **removed entirely** — option, compile definition, and all six `#if` guards | The engine is part of every build. The option is deliberately *not* kept as a no-op, so a stale `-DAV_DEV_DIFFUSION_PREVIEW` on a command line fails loudly instead of being ignored. |
| `previewTank` renamed `tank` | It was never a preview. |
| `AV_DEV_ALLPASS_CANDIDATE` default `0` → `1` | The default must be the shipped architecture. Anything else is a comparison build; `Scripts/build.ps1` warns and says not to install it. |
| `CMakePresets.json` added — `production`, `development`, `validation` | Three named, complete configurations. Every flag is stated in every preset; none is inherited. |
| `Scripts/build.ps1 -Profile` | Passes each identity-deciding flag explicitly on every configure, whether or not it differs from the CMake default. A cache can no longer decide what the plugin is. |
| `AV_COPY_PLUGIN_AFTER_BUILD=OFF` in all three presets | No build may install a binary as a side effect. |
| `Scripts/install-production-vst3.ps1` added | Installing is now a separate, deliberate, gated act. |
| `info.buildProfile` / `engineActive` / `captureCompiledIn` | The artefact reports what it is, when telemetry is compiled in at all. |

## 3. Build profiles

| Profile | Diagnostics | Capture | Allpass candidate | Post-build copy |
|---|---|---|---|---|
| `production` | OFF | OFF | 1 | OFF |
| `development` | ON | ON | 1 | OFF |
| `validation` | ON | OFF | 1 | OFF |

Production carries no telemetry code at all — not a disabled path, no thread,
no queue, no allocation. **A production build therefore reports no runtime
`build_profile`, and that is by design, not a fault.** Its identity is
established by the preset, the flags on the configure line, and the artefact
hash — not by asking it at runtime.

## 4. Installed artefact

```
C:\Program Files\Common Files\VST3\AuroraVerb.vst3
  Contents\x86_64-win\AuroraVerb.vst3
```

| | |
|---|---|
| SHA-256 | `2B451890E229F7FDC4A202898AB0384BCAAEF88E2A877FD520D5464DC9E02BD8` |
| Size | 6 012 928 bytes |
| Installed | 2026-08-23 23:30:23 |
| Built from | preset `production`, Release, `C:/JuceBuilds/av-production` |
| Tests at that source state | 2121 / 2121 passed |

Verified after installation by re-hashing the file in place.

**Only one active copy exists.** `%LOCALAPPDATA%\Programs\Common\VST3` is clean
and `C:\Program Files (x86)\Common Files\VST3` does not exist.

## 5. Rollback archive

```
%LOCALAPPDATA%\Programs\Common\VST3-AuroraVerb-backups\
    global-pre-hardening-2026-08-23_23-59-36\AuroraVerb.vst3
```

SHA-256 `A49D477DD353058287FA7FC75D4422E865497A91FC4051EB84D5E6A395622464`,
6 149 632 bytes — the artefact all Phase 5F and Phase 6 host work was carried
out on.

**This is a rollback archive only. It is not a build target and must not be
re-installed except to recover from a failure.** It is a development
configuration: telemetry and capture compiled in, engine enabled by the removed
flag. The backup root sits outside every VST3 scan path, verified
programmatically, so no host can load it by accident.

Manual rollback, from an elevated shell:

```powershell
Remove-Item 'C:\Program Files\Common Files\VST3\AuroraVerb.vst3' -Recurse -Force
Copy-Item "$env:LOCALAPPDATA\Programs\Common\VST3-AuroraVerb-backups\global-pre-hardening-2026-08-23_23-59-36\AuroraVerb.vst3" `
          'C:\Program Files\Common Files\VST3\' -Recurse -Force
```

Then confirm the hash reads `A49D477D…2464`.

## 6. Install procedure

`Scripts/install-production-vst3.ps1`, from an elevated PowerShell. It refuses
to proceed unless every gate passes, and restores the backup by itself if the
installed hash does not come out as expected — with a hard failure if the
rollback also fails.

Gates: elevation · no host process holding the old binary · source hash equals
the hash named on the command line · a readable rollback bundle exists ·
post-install hash re-verified in place.

Both the expected source hash and the rollback directory are **mandatory
parameters**. Naming the hash is the point, and install and rollback are never
separated.

## 7. Host regression — PASS

FL Studio, manual, on the installed production artefact. Eight checks, all PASS:
FDN tail present · Mix 30 % natural, perceptual parity with the previously
validated build · Mix 100 % clear tail, dry transient falls away leaving tank
character, no crackle · Decay 0.5 s clearly short, 5 s clearly long · Size
20 %↔90 % audible but slight · Mod Depth 0 %↔30 % barely audible · Width
0 %↔100 % clearly different · bypass and re-enable clean, no crackle, no gain
jump.

Two observations, neither new and neither acted on:

- **Mod Depth 0 %↔30 % is barely audible.** Consistent with the Phase 6 FU-1
  blind rounds, which could not establish an audible modulation effect either.
- **Mix 100 % feels slightly quiet, but usable.** This is the standing FU-2
  gain-staging item, not a new finding. `previewBaseTrim` is 1.0, and the
  recorded condition on raising it is unchanged: the musical case behind the
  headroom figure rests on a single program signal, and any trim above 1.0
  depends on closing that gap first. This host pass is a second program signal
  pointing the same way; it is logged, not acted on.

## 8. Follow-up found afterwards, and fixed — `build.ps1` still auto-installed

Found on 2026-08-24, during the first Phase 7 build.

`CMakePresets.json` set `AV_COPY_PLUGIN_AFTER_BUILD=OFF` in all three presets,
but **`Scripts/build.ps1` never passed that flag**, so the CMake default — which
is `ON` — applied to every build made through the script. Each one copied its
artefact into `%LOCALAPPDATA%\Programs\Common\VST3`, a folder every major DAW
scans, whatever profile it had just been built with.

This is the same class of defect the hardening pass existed to close: an
identity-deciding flag left to a default instead of stated explicitly. Section 2
above claimed "No build may install a binary as a side effect", and through this
path that claim was false.

Fixed:

- `Scripts/build.ps1` now passes `-DAV_COPY_PLUGIN_AFTER_BUILD=OFF` on every
  configure, alongside the other four flags.
- Its closing report no longer prints a plugin found in a scan folder under an
  "Installed:" heading, which read like a report of what the build had just
  done. It warns that the build did not put it there.

The one artefact this had already deposited — a Phase 7 validation build,
`3C94A95B…920D` — was removed, and all three scan paths re-verified: only
`C:\Program Files\Common Files\VST3` holds a plugin. The installed production
artefact was never touched and still hashes `2B451890…02BD8` with its original
timestamp.

## 9. Backlog — carried, not being worked

1. **Harness / plugin Early Diffusion parity off-default.** At shipping defaults
   the two are equivalent (`diffusionAmount` 0.7, `diffusionEnabled` true, Early
   Diffusion 70 % → 0.70), so no measurement in the archive is affected. They
   diverge only away from the default. **Not a blocker.** Fix before the next
   measurement campaign that sweeps Early Diffusion.
2. **Diagnostic session spam.** The development configuration opens a session
   folder per instantiation; one pluginval run leaves dozens. Unchanged by this
   work — production has no telemetry at all, so it cannot occur there.
3. **`C:\JuceBuilds\` holds several build-output bundles.** None is on a default
   scan path. If a host has a hand-added search folder, check it.
