#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real install-only regression from prebuilt artifacts; never installs on the host."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import selectors
import signal
import stat
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parent.parent
MAX_FILE = 32 * 1024 * 1024
# Historical install omissions: exercise each as a real QML load failure too.
OMITTED_QML = frozenset(('AudioDeviceDetail.qml', 'AudioDeviceList.qml',
                        'AudioApplicationMixer.qml', 'AudioAdvancedGoXLR.qml',
                        'AudioAdvancedRouting.qml', 'AudioPortList.qml',
                        'AudioDialog.qml', 'AudioSpinBox.qml'))


def file_record(path):
    before = path.lstat()
    assert stat.S_ISREG(before.st_mode) and before.st_nlink == 1
    assert before.st_size < MAX_FILE
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        def identity(s):
            return (s.st_dev, s.st_ino, s.st_mode, s.st_size, s.st_mtime_ns, s.st_ctime_ns, s.st_nlink)
        assert identity(os.fstat(fd)) == identity(before)
        value = bytearray()
        while len(value) <= before.st_size:
            part = os.read(fd, min(65536, before.st_size + 1 - len(value)))
            if not part:
                break
            value.extend(part)
        assert len(value) == before.st_size
        assert identity(os.fstat(fd)) == identity(before) == identity(path.lstat())
    finally:
        os.close(fd)
    return {'bytes': len(value), 'sha256': hashlib.sha256(value).hexdigest(),
            'mode': stat.S_IMODE(before.st_mode)}


def run(argv, env, directory, label, seconds=30):
    """Bound diagnostic pipes and preserve exact unreaped child group ownership."""
    assert Path(argv[0]).is_absolute() and len(argv) < 80
    parent = os.getpid()
    libc = ctypes.CDLL(None, use_errno=True)
    def prepare():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0 or os.getppid() != parent:
            os._exit(126)
    receipt = {'argv': argv, 'environment': env, 'cwd': str(REPO),
               'deadlineSeconds': seconds, 'diagnosticLimit': 262144}
    (directory / (label + '.command.json')).write_text(json.dumps(receipt, indent=2) + '\n')
    child = subprocess.Popen(argv, cwd=REPO, env=env, stdin=subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             start_new_session=True, preexec_fn=prepare)
    captured = bytearray()
    status = None
    deadline = time.monotonic() + seconds
    try:
        os.set_blocking(child.stdout.fileno(), False)
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            while True:
                exited = os.waitid(os.P_PID, child.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
                if exited is not None:
                    # Do not let a successful leader's descendants retain the pipe.
                    assert os.getpgid(child.pid) == child.pid
                    os.killpg(child.pid, signal.SIGKILL)
                if not selector.get_map() and exited is not None:
                    break
                if time.monotonic() >= deadline:
                    status = 124
                    break
                for key, _ in selector.select(0.025):
                    block = os.read(key.fd, 65536)
                    if not block:
                        selector.unregister(key.fileobj)
                    else:
                        captured.extend(block[:262144 + 1 - len(captured)])
                        if len(captured) > 262144:
                            status = 125
                            break
                if status is not None:
                    break
    finally:
        os.waitid(os.P_PID, child.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
        assert os.getpgid(child.pid) == child.pid
        os.killpg(child.pid, signal.SIGKILL)
        child.wait(timeout=2)
        child.stdout.close()
        (directory / (label + '.log')).write_bytes(captured[:262144])
        status = child.returncode if status is None else status
        (directory / (label + '.status')).write_text(str(status) + '\n')
    return status, captured.decode('utf-8', errors='replace')


def inventory(root):
    result = {}
    deadline = time.monotonic() + 10
    for parent, dirs, files in os.walk(root, followlinks=False):
        assert time.monotonic() < deadline and len(dirs) + len(files) < 128
        assert len(Path(parent).relative_to(root).parts) < 16
        for name in dirs:
            info = (Path(parent) / name).lstat()
            assert stat.S_ISDIR(info.st_mode) and stat.S_IMODE(info.st_mode) == 0o755
        for name in files:
            path = Path(parent) / name
            result[str(path.relative_to(root))] = file_record(path)
            assert len(result) < 128
    return result


def differences(root, expected):
    actual = inventory(root)
    return {'missing': sorted(expected.keys() - actual.keys()),
            'unexpected': sorted(actual.keys() - expected.keys()),
            'mismatched': sorted(p for p in actual.keys() & expected.keys() if actual[p] != expected[p])}, actual


def exercise(args, root, context):
    build = Path(args.build_dir).resolve(strict=True)
    assert build.is_dir() and args.gui in (0, 1)
    binaries = ['synapse-settings', 'synapse-audio-route-broker']
    if args.gui:
        binaries += ['synapse-settings-gui', 'libsynapse_settings_audio_qml.so']
    original = {name: file_record(build / name) for name in binaries}
    declarations = []
    for line in (REPO / 'gui/qml-module/qmldir').read_text().splitlines():
        fields = line.split()
        if fields and fields[-1].endswith('.qml'):
            assert len(fields) == 3 and re.fullmatch(r'[A-Za-z][A-Za-z0-9]*\.qml', fields[-1])
            declarations.append(fields[-1])
    assert declarations and len(declarations) == len(set(declarations))
    assert 'Main.qml' not in declarations
    env = {'PATH': '/usr/bin:/bin', 'LC_ALL': 'C.UTF-8', 'TZ': 'UTC',
           'SOURCE_DATE_EPOCH': '1', 'HOME': str(context / 'h'),
           'XDG_CONFIG_HOME': str(context / 'c'), 'XDG_STATE_HOME': str(context / 's'),
           'XDG_RUNTIME_DIR': str(context / 'r'), 'XDG_CACHE_HOME': str(context / 'cache'),
           'TMPDIR': str(context / 'tmp'), 'QT_QPA_PLATFORM': 'offscreen',
           'QT_QUICK_CONTROLS_STYLE': 'Basic', 'QT_QUICK_BACKEND': 'software',
           'QML_DISABLE_DISK_CACHE': '1', 'QT_FATAL_WARNINGS': '1'}
    for key, value in (('ASAN_OPTIONS', 'detect_leaks=0'), ('UBSAN_OPTIONS', 'halt_on_error=1')):
        if key in os.environ:
            assert os.environ[key] == value, 'unsupported sanitizer test environment'
            env[key] = value
    for name in ('h', 'c', 's', 'r', 'cache', 'tmp'):
        (context / name).mkdir(mode=0o700)
    # Install prebuilt artifacts only. Block all toolchain probes/rebuilds; the
    # Make test-install target owns normal CI build prerequisites, not this test.
    fixed = ['BUILD_DIR=' + str(build), 'BUILD_GUI=' + str(args.gui),
             'GUI_DEPS_AVAILABLE=1', 'CC=/usr/bin/false', 'CXX=/usr/bin/false',
             'PKG_CONFIG=/usr/bin/false', 'QMAKE6=/usr/bin/false', 'QT6_LIBEXECS=',
             'CORE_CFLAGS=', 'CORE_LIBS=', 'JSON_C_CFLAGS=', 'JSON_C_LIBS=']
    old = [arg for name in binaries for arg in ('--old-file', str(build / name))]
    cases = []
    layouts = [('usr', '/usr', '/usr/bin', '/usr/share', '/usr/lib', '/usr/lib/qt6/qml'),
               ('custom', '/opt/synapse', '/opt/synapse/libexec', '/opt/synapse/data',
                '/opt/synapse/lib64', '/opt/synapse/imports')]
    for label, prefix, bindir, datadir, libdir, qmldir in layouts:
        case = root / label
        case.mkdir(mode=0o700)
        stage = case / 'stage with spaces'
        stage.mkdir(mode=0o700)
        expected = {}
        def expect(source, destination, mode):
            assert destination.startswith('/') and '..' not in Path(destination).parts
            expected[destination[1:]] = {**file_record(source), 'mode': mode}
        expect(build / binaries[0], bindir + '/synapse-settings', 0o755)
        expect(build / binaries[1], bindir + '/synapse-audio-route-broker', 0o755)
        expect(REPO / 'data/synapse-audio-route-broker.service', libdir + '/systemd/user/synapse-audio-route-broker.service', 0o644)
        expect(REPO / 'LICENSE', datadir + '/licenses/synapse-settings/LICENSE', 0o644)
        module = qmldir + '/Synapse/Settings/Audio'
        if args.gui:
            expect(build / 'synapse-settings-gui', bindir + '/synapse-settings-gui', 0o755)
            expect(build / 'libsynapse_settings_audio_qml.so', module + '/libsynapse_settings_audio_qml.so', 0o755)
            for name in ('qmldir', 'synapse-settings-audio.qmltypes'):
                expect(REPO / 'gui/qml-module' / name, module + '/' + name, 0o644)
            for name in declarations:
                expect(REPO / 'gui/qml' / name, module + '/' + name, 0o644)
            expect(REPO / 'data/org.synapse.Settings.desktop', datadir + '/applications/org.synapse.Settings.desktop', 0o644)
        argv = [args.make, '--no-print-directory', '-j1', '-f', args.makefile, *old, *fixed,
                'PREFIX=' + prefix, 'BINDIR=' + bindir, 'DATADIR=' + datadir,
                'LIBDIR=' + libdir, 'QMLDIR=' + qmldir, 'DESTDIR=' + str(stage), 'install']
        code, output = run(argv, env, case, 'install')
        assert code == 0, output
        delta, actual = differences(stage, expected)
        result = {'layout': label, 'gui': bool(args.gui), 'expected': expected,
                  'installed': actual, 'differences': delta, 'qml': [],
                  'omissionsRejected': [], 'qmlOmissionsRejected': []}
        cases.append(result)
        (root / 'results.json').write_text(json.dumps(cases, indent=2) + '\n')
        if args.gui:
            # The input has no source-relative feature imports. Only the newly
            # installed module is added to a clean runner's module search paths.
            probe = case / 'tst_installed_audio.qml'
            probe.write_bytes((REPO / 'tests/qml/tst_installed_audio.qml').read_bytes())
            for locale in ('en_US', 'it_IT'):
                qenv = {**env, 'LC_ALL': locale + '.UTF-8'}
                qargv = [args.qmltestrunner, '-nocrashhandler', '-import', str(stage / qmldir[1:]), '-input', str(probe)]
                code, output = run(qargv, qenv, case, 'qml-' + locale)
                passed = code == 0 and bool(re.search(r'Totals: [1-9][0-9]* passed, 0 failed, 0 skipped, 0 blacklisted', output))
                result['qml'].append({'locale': locale, 'status': code, 'passed': passed})
        (root / 'results.json').write_text(json.dumps(cases, indent=2) + '\n')
        assert not any(delta.values()), json.dumps(delta, sort_keys=True)
        assert all(row['passed'] for row in result['qml']), 'installed module construction failed; see retained QML logs'
        for name in declarations if args.gui else ():
            path = stage / module[1:] / name
            hidden = case / ('omitted-' + name)
            path.rename(hidden)
            try:
                missing, _ = differences(stage, expected)
                assert missing == {'missing': [module[1:] + '/' + name], 'unexpected': [], 'mismatched': []}
                result['omissionsRejected'].append(name)
                if name in OMITTED_QML:
                    code, output = run(qargv, {**env, 'LC_ALL': 'en_US.UTF-8'}, case, 'missing-' + name)
                    assert code not in (0, 124, 125) and name in output and 'No such file or directory' in output
                    result['qmlOmissionsRejected'].append(name)
            finally:
                assert not path.exists()
                hidden.rename(path)
        assert not any(differences(stage, expected)[0].values())
        (root / 'results.json').write_text(json.dumps(cases, indent=2) + '\n')
    assert {name: file_record(build / name) for name in binaries} == original
    assert not any((context / 's').iterdir()) and not any((context / 'r').iterdir())
    print('install-regression: PASS layouts=2 gui=' + str(args.gui)
          + ' qml=' + str(len(declarations) if args.gui else 0)
          + ' omission-checks=' + str(sum(len(c['omissionsRejected']) for c in cases))
          + ' prebuilt-inputs-unchanged no-Audio-activation')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--gui', type=int, choices=(0, 1), required=True)
    parser.add_argument('--make', default='/usr/bin/make')
    parser.add_argument('--makefile', default=str(REPO / 'Makefile'))
    parser.add_argument('--qmltestrunner', default='/usr/lib/qt6/bin/qmltestrunner')
    parser.add_argument('--output-dir')
    args = parser.parse_args()
    os.umask(0o077)
    with tempfile.TemporaryDirectory(prefix='si-', dir='/tmp') as private:
        context = Path(private)
        if args.output_dir:
            root = Path(args.output_dir).absolute()
            root.mkdir(mode=0o700)
            exercise(args, root, context)
        else:
            with tempfile.TemporaryDirectory(prefix='settings-install-', dir='/tmp') as name:
                exercise(args, Path(name), context)


if __name__ == '__main__':
    main()
