#!/usr/bin/env python3
"""Build and run the firmware's PC tests.

    python test/host/run_tests.py [ledmap.json]

Compiles lib/transit_core + nanopb with the system C++ compiler (the same
sources the ESP32 builds), runs host_test against an LED map (default: the
the map in data/ledmap.json), then, if Node.js and the live preview
are available, cross-checks every LED choice against the preview's engine.
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.normpath(os.path.join(HERE, '..', '..'))


def find_arduinojson():
    for root in ('.pio/libdeps/transitmap/ArduinoJson/src', '.pio/libdeps/transitmap_sim/ArduinoJson/src'):
        p = os.path.join(FW, root)
        if os.path.exists(os.path.join(p, 'ArduinoJson.h')):
            return p
    dst = os.path.join(tempfile.gettempdir(), 'ArduinoJson-v7.4.3')
    if not os.path.exists(dst):
        subprocess.check_call(['git', 'clone', '-q', '--depth', '1', '-b', 'v7.4.3',
                               'https://github.com/bblanchon/ArduinoJson', dst])
    return os.path.join(dst, 'src')


def main():
    ledmap = sys.argv[1] if len(sys.argv) > 1 else os.path.join(FW, 'data', 'ledmap.json')
    out = tempfile.mkdtemp(prefix='transit-host-')
    exe = os.path.join(out, 'host_test')
    core = os.path.join(FW, 'lib', 'transit_core', 'src')
    pb = os.path.join(FW, 'lib', 'nanopb')
    srcs = [os.path.join(HERE, 'host_test.cpp')]
    srcs += [os.path.join(core, f) for f in sorted(os.listdir(core)) if f.endswith('.cpp')]
    srcs += [os.path.join(pb, f) for f in sorted(os.listdir(pb)) if f.endswith('.c')]
    cxx = os.environ.get('CXX', 'g++')
    cmd = [cxx, '-std=gnu++17', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
           '-fsanitize=address,undefined', '-I', core, '-I', pb, '-isystem', find_arduinojson(), '-Wno-maybe-uninitialized', '-o', exe]
    # nanopb's .c files go through the C++ compiler too; that's fine and keeps one command.
    cmd += ['-x', 'c++'] + srcs
    print('building host test…')
    subprocess.check_call(cmd)
    print('running against', ledmap)
    rc = subprocess.call([exe, ledmap, out])
    preview = os.path.normpath(os.path.join(FW, '..', 'live-preview'))
    if shutil.which('node') and os.path.exists(os.path.join(preview, 'js', 'engine.js')):
        print('cross-checking against the live preview engine…')
        rc |= subprocess.call(['node', os.path.join(HERE, 'crosscheck.js'), preview, ledmap, out])
    else:
        print('(skipping preview cross-check: needs node and ../live-preview)')
    print('PASS' if rc == 0 else 'FAIL')
    return rc


if __name__ == '__main__':
    sys.exit(main())
