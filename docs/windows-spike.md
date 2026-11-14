# Windows spike: ArdiaCore on Windows with a shell page

A spike to decide how Ardia runs on Windows, in the shape of the ArdiaCore spike (`docs/ardiacore-spike.md`): build
the smallest thing that proves the risky part, run automated checks against it, and end with a verdict (GO,
GO-WITH-CHANGES or NO-GO). Nothing here ships. The Mac app does not change.

## Findings so far (2026-11-13)

Checked before any build:

| Assumption | Result |
| --- | --- |
| Helium ships Windows at our pin | **Yes.** `imputnet/helium-windows` 0.21.2.1 builds on helium-chromium `e817502a`, the commit `engine/patches/build/helium-pins.ini` pins, at Chromium 157.0.8103.50. It adds 26 patches of its own (`patches/series`) |
| Our series applies on top of it | **Yes.** `spikes/windows/patch-check.py` on a fresh Chromium 157.0.8103.50 checkout for `target_os = ['win']`: helium-chromium 339 applied and 6 skipped, helium-windows 17 applied and 9 skipped (its remote build and WinSparkle updater patches), all with no fuzz; every 03-helium stage passes; 77 of our 78 patches apply strictly (git apply, no fuzz) and both layers (`engine/chromium`, `engine/ardiacore`, hooks included) apply. The one left, `chromium-safe-storage-no-silent-reset.patch`, fails a single hunk in `keychain_key_provider.mm`, a Mac-only file whose context comes from helium-macos's `prevent-reset-on-keychain-unavailability.patch`; Windows never compiles it. The three files both series touch (`chrome/BUILD.gn`, `chrome/browser/BUILD.gn`) merged cleanly |
| Our build tools run on Windows | **With two fixes.** `ardia_namesub.py` forced the `fork` start method, which Windows lacks, and its Ardia rules only reached forked workers: it now hands each spawned worker the same rules (`initializer=helium_utils`), and leaves the Mac on `fork`. The layers' `apply.sh` need `rsync`, which Git for Windows doesn't ship; the spike driver puts a stand-in on `PATH`. `pathguard.py` (the build lock and the guard on `~/chromium-build`) imports `pwd` and runs `lockf`, so it doesn't load on Windows; the driver stubs it, and a Windows build machine needs its own lock |
| WinSparkle for updates | **Already there.** Helium's Windows updater is WinSparkle (`helium/windows/updater/*`); its feed has to be emptied as on the Mac (`chromium-helium-branding.patch`) and the app's updater pointed at our appcast |
| Chrome encrypts secrets with DPAPI | **Yes, unless `--disable-encryption` is passed.** ungoogled's `windows-disable-encryption` turns encryption off only behind that switch, so the profile gate (`ac_main_delegate.cc`) must drop it on a real profile, as it drops the other switches that weaken one |
| Our patches are platform-neutral | **Mostly.** `chromium-webstore-direct.patch` sends `os=mac` to the Web Store; it has to name the real platform. `chromium-helium-rlz-for-cdm-storage-id.patch` reasons about the Mac's machine id; Windows needs its own look at `windows-disable-rlz` |
| Our engine targets load on Windows | **Yes, after taking the Mac out of their GN files.** Both `BUILD.gn` files opened with `assert(is_mac)`. `//chrome/browser/ardia` now lists its two `.mm` files and its frameworks under `is_mac`, and `//ardia/core` keeps `launch_switches` and `hooks` for every platform, with `core` and its exports under `is_mac`. Neither is wired into a Windows target yet (their deps are in the Mac's `chrome_dll`), so the spike pulls `//chrome/browser/ardia:ardia` in through `root_extra_deps` (`spikes/windows/args.gn`). `gn gen` passes: 36,307 targets |
| Our engine code compiles on Windows | **40 of 42 files, after small fixes.** First build (1 h 52 min, `spikes/windows/args.gn`): Chromium itself compiled clean. Ours needed (1) `FilePath` built from and read as UTF-8 (`FromUTF8Unsafe`, `AsUTF8Unsafe`) in six files, where Windows paths are UTF-16 and the Mac's are already UTF-8; (2) `components_branded_strings.h` in `ac_autofill.cc`, where Windows keeps the card reauth prompt; (3) `SignedForDistribution()` split into `ac_launch_switches_mac.cc` (unchanged) and `ac_launch_switches_win.cc` (Authenticode through `WinVerifyTrust`; only an unsigned binary counts as a dev build, so a bad or tampered signature fails closed; it doesn't yet pin Ardia's publisher), and the test gate's mock-keychain rule kept to the Mac, since Chrome's DPAPI key lives in the scratch profile's own Local State. Then the two security files got Windows versions, each split Chromium-style with the Mac code moved unchanged into `_posix.cc`: the control token file (`ac_cli_control_file_win.cc`: `CREATE_NEW`, a protected DACL for the current user only, no inheritable handle, `FlushFileBuffers`, `MoveFileExW` under the publish lock) and the Scripts folder checks (`ac_scripts_paths_win.cc`: `GetFinalPathNameByHandleW` for real paths, reparse points as not plain, and UNC, device, drive-relative and root-relative paths, streams, trailing dots and spaces, device names and backslashes in manifest paths all failing closed; a resolved prefix off the local drives fails closed in the shared code too) |
| `chrome.dll` links with our code | **Yes.** `chromium-ardia-layer.patch` gives the Windows `chrome_dll` the dependency on `//chrome/browser/ardia` the Mac's has. The link then wanted the PiP helpers (`pip/ac_pip_native.h`, was `ac_pip_mac.h`; `pip/ac_pip_win.cc` keeps bounds as JSON and fades through the aura layer) and three ArdiaCore window queries, which answer false on Windows until ArdiaCore's window layer is ported (`ac_host_visibility_win.cc`; false fails closed at every token-policy caller). `chrome.exe` with our code runs a page headless on a scratch profile. The `ac_*` calls aren't exported from `chrome.dll` yet (the Mac's `ac_engine.exports` is an ld list; Windows needs a `.def`), so no app can call them |
| The Windows security code holds up | **After a review round.** An independent review of `ac_cli_control_file_win.cc` and `ac_scripts_paths_win.cc` found no direct bypass of the Scripts policy, and three medium findings, each reproduced on this machine before the fix: (1) a junction whose target denies read-attributes made `RealPath` fail while the manifest still loaded through it, and the walks skipped it as missing (`ResolvePath` now says missing or unreadable, and unreadable fails closed, on POSIX too for errors other than `ENOENT`/`ENOTDIR`); (2) the token file was readable by low-integrity processes of the same user (now labelled Medium with no-read-up, `S:(ML;;NRNW;;;ME)`, which `icacls` shows; a drive that drops file permissions refuses the file); (3) the plain Scripts folder was refused when the profile path differs in case, short names or links from the on-disk form (`scripts_paths::Canonical` resolves the data dir once at startup on Windows). Also fixed: a zero-byte write could publish a truncated token file; paths off the local drives aren't opened just to classify them; an unpacked extension whose real path is the Scripts folder counts as Scripts under any name (this one changes the Mac too). Left as decisions: every unpacked load from a UNC, mapped or device path is refused, with the Scripts error message; and the `ardia` command's reader (`apps/cli/ardia_cli/endpoint.py`) still checks the file the POSIX way, so it needs a Windows owner and DACL check before the CLI works there |
| Our tools keep Chromium's line endings | **Now.** The `apply.sh` hook helper, 03-helium's fixups and version stages and `ardia_namesub.py` wrote through Python text mode, which writes CRLF on Windows: 2,245 Chromium files came out CRLF, which breaks `git apply` (so `series.py check` and patch edits). They now write with `newline=''` (no change on the Mac), and the spike driver turns back to LF what Helium's own string tools write |
| This machine can build Chromium 157 | **Toolchain yes, build not tried.** Chromium 157 wants Visual Studio 2026 (`MSVS_VERSIONS` in `build/vs_toolchain.py`), Windows SDK 10.0.28000 and SDK Debugging Tools 10.0.26100.3323 or later: installed VS Community 2026 18.12 (C++ desktop, ATL/MFC) and SDK 10.0.28000.2957 with its debuggers. The tree is in `D:\chromium-build` (ReFS on a USB SSD, not the NTFS Chromium's docs name); checkout took 11 minutes and `gclient runhooks` (with `DEPOT_TOOLS_WIN_TOOLCHAIN=0`) passed. 16 cores and 64 GB |

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
