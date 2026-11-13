#!/usr/bin/env python3
"""Windows spike, step 1: does Ardia's engine series apply to Chromium with Helium for Windows?

On a fresh chromium/src checkout made for target_os win, it runs 03-helium.py's stages with helium-windows in place
of helium-macos, then every line of engine/patches/series, and goes on past a failure so the report names every
step that needs Windows work. Strict means git apply with no fuzz, as series.py check; fuzz means GNU patch took it.

Usage: patch-check.py --tree SRC --helium DIR --helium-windows DIR --work DIR
"""
import argparse
import importlib.util
import os
import pathlib
import shutil
import subprocess
import sys
import types

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
BUILD = REPO / 'engine/patches/build'

WINDOWS_SKIP = {
    'helium/windows/rbe-linux-workers.patch': 'Helium\'s remote build setup',
    'helium/windows/updater/build-wiring.patch': 'WinSparkle inside chrome.dll; the app has its own updater',
    'helium/windows/updater/glue.patch': 'WinSparkle inside chrome.dll',
    'helium/windows/updater/helper.patch': 'WinSparkle inside chrome.dll',
    'helium/windows/updater/register.patch': 'WinSparkle inside chrome.dll',
    'helium/windows/updater/update-notifications.patch': 'WinSparkle inside chrome.dll',
    'helium/windows/updater/winsparkle-api-extensions.patch': 'WinSparkle inside chrome.dll',
    'helium/windows/updater/winsparkle-drop-3rdparty-deps.patch': 'WinSparkle inside chrome.dll',
    'helium/windows/updater/winsparkle-headless-ui.patch': 'WinSparkle inside chrome.dll',
}


def stub_pathguard():
    guard = types.ModuleType('pathguard')
    guard.reaches = lambda *a, **k: None
    guard.overlaps = lambda *a, **k: False
    guard.real_lock_held = lambda: False
    guard.chromium_lock_held = lambda: False
    guard.real_chromium_build = lambda: str(pathlib.Path.home() / 'chromium-build')
    for name in ('require_writable', 'require_tree_writable', 'require_scratch_destination'):
        setattr(guard, name, lambda *a, **k: None)
    sys.modules['pathguard'] = guard


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def patch_tool():
    found = shutil.which('patch')
    if found:
        return found
    git = shutil.which('git')
    if git:
        candidate = pathlib.Path(git).resolve().parent.parent / 'usr/bin/patch.exe'
        if candidate.exists():
            return str(candidate)
    sys.exit('no GNU patch: install Git for Windows or put patch on PATH')


def first_lines(text, limit=400):
    return ' | '.join(l.strip() for l in text.splitlines() if l.strip())[:limit]


def helium_stage(helium, ctx, rows):
    for repo, tag in ((ctx.hc, 'helium-chromium'), (ctx.hm, 'helium-windows')):
        for name in helium.series(repo):
            if name in helium.SKIP:
                rows.append((tag, name, 'skipped', helium.SKIP[name]))
                continue
            path, dropped = helium.without_omitted(ctx, name, repo / 'patches' / name)
            dry = helium.run_patch(ctx, path, dry=True)
            if dry.returncode:
                rows.append((tag, name, 'FAILED', first_lines(dry.stdout + dry.stderr)))
                continue
            done = helium.run_patch(ctx, path, dry=False)
            status = 'applied' if done.returncode == 0 else 'FAILED'
            detail = ('offset' if 'offset' in done.stdout else 'exact') if done.returncode == 0 else \
                first_lines(done.stdout + done.stderr)
            if dropped:
                detail += '; without ' + ', '.join(dropped)
            rows.append((tag, name, status, detail))
    for stage in ('fixups', 'domsub', 'namesub', 'brand-namesub', 'version'):
        try:
            helium.HANDLERS[stage](ctx)
            rows.append(('03-helium', stage, 'applied', ''))
        except helium.StageError as e:
            rows.append(('03-helium', stage, 'FAILED', first_lines(str(e))))


def git(tree, *args):
    return subprocess.run(['git', *args], cwd=tree, capture_output=True, text=True)


RSYNC_STAND_IN = '''#!/usr/bin/env bash
set -euo pipefail
paths=(); for a in "$@"; do case "$a" in -*) ;; *) paths+=("$a");; esac; done
src="${paths[0]%/}"; dst="${paths[1]%/}"
rm -rf "$dst"; mkdir -p "$(dirname "$dst")"; cp -r "$src" "$dst"
'''

MAC_ONLY_PARTS = ('_mac.', '_mac_', '/apple/', '/mac/', '/cocoa/')


def mac_only(path):
    return path.endswith('.mm') or any(part in path for part in MAC_ONLY_PARTS)


def layer_env(tree, work):
    env = dict(os.environ, CHROMIUM_SRC=str(tree))
    if shutil.which('rsync'):
        return env
    shim = work / 'bin'
    shim.mkdir(parents=True, exist_ok=True)
    (shim / 'rsync').write_text(RSYNC_STAND_IN, newline='\n')
    env['PATH'] = str(shim) + os.pathsep + env['PATH']
    return env


def failed_files(output):
    current, failed = None, set()
    for line in output.splitlines():
        if line.startswith('checking file '):
            current = line[len('checking file '):].strip()
        elif 'FAILED' in line and current:
            failed.add(current)
    return failed


def series_stage(series, tree, patch, work, rows):
    bash = shutil.which('bash')
    env = layer_env(tree, work)
    for step in series.read_series():
        if step.kind == 'layer':
            script = REPO / step.name / 'apply.sh'
            p = subprocess.run([bash, str(script)], cwd=REPO, capture_output=True, text=True, env=env)
            rows.append(('series', step.name, 'applied' if p.returncode == 0 else 'FAILED',
                         '' if p.returncode == 0 else first_lines(p.stderr or p.stdout)))
            continue
        if git(tree, 'apply', '--check', '--whitespace=nowarn', step.patch).returncode == 0:
            git(tree, 'apply', '--whitespace=nowarn', step.patch)
            rows.append(('series', step.name, 'applied', 'strict'))
            continue
        dry = subprocess.run([patch, '-p1', '--forward', '--dry-run', '-i', step.patch], cwd=tree,
                             capture_output=True, text=True)
        if dry.returncode:
            failed = failed_files(dry.stdout)
            only_mac = bool(failed) and all(mac_only(f) for f in failed)
            rows.append(('series', step.name, 'mac-only' if only_mac else 'FAILED',
                         'hunks fail in ' + ', '.join(sorted(failed)) if failed else first_lines(dry.stdout + dry.stderr)))
            continue
        subprocess.run([patch, '-p1', '--forward', '-s', '--no-backup-if-mismatch', '-i', step.patch], cwd=tree,
                       capture_output=True, text=True)
        rows.append(('series', step.name, 'fuzz', first_lines(dry.stdout)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tree', type=pathlib.Path, required=True)
    parser.add_argument('--helium', type=pathlib.Path, required=True)
    parser.add_argument('--helium-windows', type=pathlib.Path, required=True)
    parser.add_argument('--work', type=pathlib.Path, required=True)
    args = parser.parse_args()
    tree = args.tree.resolve()
    if not (tree / 'chrome/VERSION').exists():
        parser.error(f'{tree} is not a chromium/src tree')
    if (tree / 'chrome/browser/ungoogled_flag_choices.h').exists():
        parser.error(f'{tree} is already patched; this runs once, on a fresh checkout')
    args.work.mkdir(parents=True, exist_ok=True)

    stub_pathguard()
    sys.path.insert(0, str(BUILD))
    helium = load('helium_step', BUILD / '03-helium.py')
    helium.PATCH = patch_tool()
    helium.SKIP = {**helium.SKIP, **WINDOWS_SKIP}
    series = load('series', REPO / 'engine/patches/series.py')

    ctx = argparse.Namespace(tree=tree, hc=args.helium.resolve(), hm=args.helium_windows.resolve(), no_brand=False,
                             guard_extra=[], work=args.work.resolve())
    ctx.domsub_cache = ctx.work / 'domsubcache.tar.gz'
    ctx.report = ctx.work / 'helium-patches.tsv'

    rows = []
    helium_stage(helium, ctx, rows)
    series_stage(series, tree, helium.PATCH, ctx.work, rows)

    report = ctx.work / 'patch-check.tsv'
    report.write_text('source\tstep\tstatus\tdetail\n' + ''.join('\t'.join(r) + '\n' for r in rows), encoding='utf-8')
    counts = {}
    for source, _, status, _ in rows:
        counts.setdefault(source, {}).setdefault(status, 0)
        counts[source][status] += 1
    for source, by_status in counts.items():
        print(f'{source}: ' + ', '.join(f'{n} {s}' for s, n in sorted(by_status.items())))
    for source, step, status, detail in rows:
        if status in ('FAILED', 'fuzz', 'mac-only'):
            print(f'{status}\t{source}\t{step}\t{detail[:200]}')
    print(f'report: {report}')
    return 1 if any(r[2] == 'FAILED' for r in rows) else 0


if __name__ == '__main__':
    sys.exit(main())
