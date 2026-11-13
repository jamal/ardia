# Windows spike: ArdiaCore on Windows with a shell page

A spike to decide how Ardia runs on Windows, in the shape of the ArdiaCore spike (`docs/ardiacore-spike.md`): build
the smallest thing that proves the risky part, run automated checks against it, and end with a verdict (GO,
GO-WITH-CHANGES or NO-GO). Nothing here ships. The Mac app does not change.

## Findings so far (2026-11-13)

Checked before any build:

| Assumption | Result |
| --- | --- |
| Helium ships Windows at our pin | **Yes.** `imputnet/helium-windows` 0.21.2.1 builds on helium-chromium `e817502a`, the commit `engine/patches/build/helium-pins.ini` pins, at Chromium 157.0.8103.50. It adds 26 patches of its own (`patches/series`) |
| Our series applies on top of it | **Likely, with small fixes.** Only three files are touched by both: `chrome/BUILD.gn` (ours: `chromium-ardia-layer`, `chromium-angle-metal-shader-cache`; theirs: `windows-disable-reorder-fix-linking`) and `chrome/browser/BUILD.gn` (ours: `chromium-helium-onboarding-off`; theirs: two updater patches) |
| WinSparkle for updates | **Already there.** Helium's Windows updater is WinSparkle (`helium/windows/updater/*`); its feed has to be emptied as on the Mac (`chromium-helium-branding.patch`) and the app's updater pointed at our appcast |
| Chrome encrypts secrets with DPAPI | **Yes, unless `--disable-encryption` is passed.** ungoogled's `windows-disable-encryption` turns encryption off only behind that switch, so the profile gate (`ac_main_delegate.cc`) must drop it on a real profile, as it drops the other switches that weaken one |
| Our patches are platform-neutral | **Mostly.** `chromium-webstore-direct.patch` sends `os=mac` to the Web Store; it has to name the real platform. `chromium-helium-rlz-for-cdm-storage-id.patch` reasons about the Mac's machine id; Windows needs its own look at `windows-disable-rlz` |
| This machine can build Chromium 157 | **Not yet.** Chromium 157 wants Visual Studio 2026 (`MSVS_VERSIONS` in `build/vs_toolchain.py`), Windows SDK 10.0.28000 and SDK Debugging Tools 10.0.26100.3323 or later. Installed: VS 2022 17.14 and SDKs up to 10.0.26100. Disk: Chromium asks for 100 GB free on NTFS; C: (internal NVMe) has 84 GB, D: is ReFS on USB. CPU (16 cores) and RAM (64 GB) are enough |

## The question

On the Mac a tab's `WebContentsViewCocoa` is an NSView inside React Native's NSViews, so the UI draws over pages
(command bar, Peek, prompts, PiP controls) because AppKit stacks views. On Windows Chrome composites the whole window
itself (Aura, viz, DirectComposition): there is no per-tab native view to place inside someone else's view tree.

The spike tests one answer: **Chrome draws everything.** Each window is a Chrome Views widget, as on the Mac, and the
app's React UI runs as a react-native-web bundle in a trusted WebUI `WebContents` layered around and over the tabs'
`WebContents`. Chrome composites shell and pages in one frame.

The alternative, react-native-windows hosting Chrome's output, is out of the spike unless this one fails
(The fallback, below).

## What carries over unchanged

| Part | Why it ports |
| --- | --- |
| `apps/browser/src` (~97k lines TS) | `packages/demo` already runs it on react-native-web with ~400 lines of shims |
| `packages/core`, `packages/sync/src` | Pure TS |
| `engine/chromium` (the `ac_*` calls) | `.cc` apart from 237 lines of `.mm` |
| `engine/patches` | 71 of 81 patches touch no Mac-only Chromium file |
| The JS ↔ engine API | `engine.call("domain.verb", json)` is string-keyed JSON, so it crosses a WebUI message handler as it crosses an Expo module |

## What to build

1. **The engine on Windows.** Check out Chromium 157.0.8103.50 on a Windows machine with its own build directory
   (`out/Release_GN_x64`), apply Helium's Windows patches and `engine/patches/series`, and build `chrome`. First confirm
   Helium's Windows patch set matches the Helium version in `docs/engine-build.md`; list every patch of ours that
   fails to apply or build, with why.
2. **ArdiaCore's window on Aura.** `WindowHost` on `DesktopNativeWidgetAura` instead of `NativeWidgetMac`: one widget,
   two profiles each with its own `Browser` and `ACBrowserWindow` (no `BrowserView`, tab strip or toolbar), a custom
   caption with Snap Layouts hit-testing, and a Mica backdrop. Mac-only files get a `_win.cc` sibling in Chromium's
   naming; shared logic moves out of `.mm` into `.cc` only where the spike needs it.
3. **The shell page.** A `chrome://ardia-shell` WebUI serving the demo's build of `apps/browser/src`, in a
   `views::WebView` that covers the window with a transparent background. Tab `WebContents` sit in the content card's
   rectangle, which the shell reports on every layout. Hit-testing sends the card's rectangle to the page unless the
   shell has something over it (an open command bar, a prompt).
4. **One message handler.** `ArdiaShellHandler` takes `call(domain.verb, json)` and `on(domain.event)` and dispatches
   to the existing `ac_*` table. A `platform/` backend for the shell (`platform/backends/shell/…`) answers the domains
   the spike needs: engine, tabs, windows, webviews, profiles, menus. Everything else answers "not on Windows yet".
5. **A hidden test instance.** `ARDIA_BACKGROUND=1` on Windows: the window is cloaked with `DWMWA_CLOAK`, never
   activated (`WS_EX_NOACTIVATE`, `SW_SHOWNOACTIVATE`), and out of Alt+Tab. Captures come from CDP
   (`Page.captureScreenshot` on the shell and on each tab) and from Chrome's own compositor readback.

## Acceptance checks

Each one is a scripted check against a hidden instance, as in `packages/ardiacore/scripts/acceptance.mjs`.

| Check | Passes when |
| --- | --- |
| `boot` | Process starts through `ChromeMain` with our main delegate; the shell page loads and renders the sidebar |
| `two-profiles` | One window, two profiles; paging swaps the active `Browser`, the window stays |
| `tab-in-card` | A tab's page draws inside the content card's rectangle and follows window resize with no frame lag |
| `overlay` | The command bar draws over a page and takes clicks; with it closed, the same point clicks the page |
| `popups` | `window.open`, `target=_blank`, Ctrl-click and a POST form each arrive as our tabs with their navigation intact |
| `chrome-bubbles` | Chrome's autofill dropdown, a permission prompt and an extension's popup attach to our window |
| `devtools` | Docked DevTools beside the page |
| `focus` | Typing goes to the shell's field, then to a page field after a click, then back on Ctrl+L, with no lost keys |
| `shortcuts` | Ctrl+T, Ctrl+W, Ctrl+L and Ctrl+Shift+T reach the commands catalog from both shell and page focus |
| `quit` | Close and quit, including a cancelled `beforeunload`, end with no leaked process |
| `hidden` | The instance never becomes the foreground window and never shows over another app |

## Measurements

Each runs on the same machine, interleaved with its control, one at a time (AGENTS.md).

| Measure | Control | Bar |
| --- | --- | --- |
| Idle CPU with one tab open | Chrome on the same build | Under 0.5%, the README's claim |
| Memory, one window, one tab | Chrome on the same build | Report the shell process's cost |
| Sidebar open/close and tab switch animation, frames dropped at 60 and 120 Hz | The Mac app's frame timing for the same animation | No dropped frames |
| Keypress to paint in the command bar | The Mac app | Within one frame of the Mac |
| Cold start to first paint | The Mac app on its machine, as a reference | Report |

## Verdict

- **GO** when every check passes and every measurement meets its bar.
- **GO-WITH-CHANGES** when a check or bar fails for a reason with a named fix (move an animation from `Animated` to
  CSS, a hook in Chrome's code, a shell process that is shared across windows).
- **NO-GO** when overlays, focus or animation frame timing can't reach the Mac's level in a shell page. Then the
  fallback below gets its own spike.

The verdict and its numbers go at the top of this file, as in `docs/ardiacore-spike.md`.

## The fallback

react-native-windows (Fabric, WinAppSDK composition) hosting Chrome's output. Chrome would first need a patch to
render a tab into a visual the app supplies (what Edge does for WebView2's composition mode), and RN overlays would
have to share a visual tree with Chrome's DirectComposition output. None of the react-native-macos patches in
`patches/` carry over.

## Not in the spike

Sync, import, downloads, passwords UI, PiP, shaders, the updater, the installer, signing, the CLI and telemetry. Their
Windows counterparts, for the plan after a GO:

| Mac | Windows |
| --- | --- |
| Keychain, Safe Storage | Chrome's OSCrypt (DPAPI, App-Bound Encryption) |
| Sparkle | WinSparkle, on the same appcast |
| Notarization | Authenticode |
| WebKit page engine for DRM sites | WebView2 behind `PageEngine` |
| Metal shaders (`packages/shaders`) | WGSL on WebGPU in the shell; the demo's `gl` shims are the start |
| SF Symbols | The demo's icon set; Apple's license keeps SF Symbols on Apple platforms |
| iCloud Drive sync folder | The sync folder as a plain folder, as on Android; iCloud for Windows works |
| Menu bar | A window menu from `state/commands/commandCatalog.ts`, Ctrl in place of ⌘ |
| `scripts/agent` hidden instances, `screencapture`, `lsappinfo` | Cloaked windows, CDP captures, a foreground-window check |

AGENTS.md and `docs/agent-brief.md` describe Mac tooling only (`open -g`, `/Applications`, `~/chromium-build`). A Windows
build machine needs its own rules there before agents work on it.
