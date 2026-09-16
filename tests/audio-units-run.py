#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline C Audio characterization: fixed fixture, bounded calls, exact traces."""
import ctypes
import json
import os
from pathlib import Path
import resource
import selectors
import signal
import stat
import subprocess
import sys
import tempfile
import time


def write_json(path, value):
    path.write_text(json.dumps(value, separators=(",", ":")) + "\n", encoding="utf-8")


def fixture():
    root = Path(os.environ["SYNAPSE_AUDIO_UNITS_STATE"])
    assert root.is_absolute() and stat.S_IMODE(root.stat().st_mode) == 0o700
    assert os.getpid() == os.getpgrp()
    assert os.environ["LC_ALL"] == os.environ["LANG"] == "C"
    assert os.readlink("/proc/self/fd/0") == os.readlink("/proc/self/fd/2") == "/dev/null"
    assert os.readlink("/proc/self/fd/1").startswith("pipe:")
    args = sys.argv[1:]
    with (root / "trace").open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(args) + "\n")
    data = json.loads((root / "state.json").read_bytes())
    mode = os.environ.get("SYNAPSE_AUDIO_UNITS_MODE", "success")
    if args == ["--format=json", "info"]:
        if mode == "leader-descendant":
            child = os.fork()
            if child == 0:
                for descriptor in (0, 1, 2):
                    os.close(descriptor)
                time.sleep(10)
                os._exit(0)
            write_json(root / "descendant.json", {"pid": child, "group": os.getpgrp()})
        print('{"default_sink_name":"sink.alpha","default_source_name":"source.alpha"}')
        return
    if len(args) == 3 and args[:2] == ["--format=json", "list"]:
        category = args[2]
        assert category in ("cards", "sinks", "sources", "sink-inputs", "source-outputs")
        if mode == "postflight-unavailable" and data["writes"]:
            raise SystemExit(65)
        if category == data["category"]:
            data["reads"] += 1
            entry = data[category][0] if data[category] else None
            if mode == "drift" and data["reads"] == 2:
                entry[data["field"]] = data["next"]
            if data["writes"] == 1:
                data["afterReads"] += 1
                if data["afterReads"] == 2 and mode in ("external", "intervene"):
                    entry[data["field"]] = data["old"] if mode == "external" else data["third"]
        write_json(root / "state.json", data)
        print(json.dumps(data[category], separators=(",", ":")))
        return
    operations = {
        "set-sink-volume": ("sinks", "volume"), "set-source-volume": ("sources", "volume"),
        "set-sink-mute": ("sinks", "mute"), "set-source-mute": ("sources", "mute"),
        "set-card-profile": ("cards", "active_profile"),
        "set-sink-port": ("sinks", "active_port"), "set-source-port": ("sources", "active_port")}
    assert len(args) == 3 and args[0] in operations
    category, field = operations[args[0]]
    assert category == data["category"] and field == data["field"]
    entry = data[category][0]
    assert entry["name"] == args[1]
    data["writes"] += 1
    count = data["writes"]
    requested = args[2]
    if field == "volume":
        assert requested.endswith("%") and requested[:-1].isdecimal()
        requested = {"mono": {"value": (int(requested[:-1]) * 65536 + 50) // 100}}
    elif field == "mute":
        assert requested in ("0", "1")
        requested = requested == "1"
    else:
        assert requested in ("choice.a", "choice.b", "choice.c")
    if mode == "fail" or (mode == "rollback-fail" and count == 2):
        write_json(root / "state.json", data)
        raise SystemExit(65)
    if mode != "no-mutate" or count != 1:
        entry[field] = requested
    if count == 1 and mode == "identity":
        entry["index"] += 1000
    if count == 1 and mode == "vanish":
        data[category] = []
    write_json(root / "state.json", data)
    if count == 1 and mode in ("fail-after", "external", "intervene", "rollback-fail"):
        raise SystemExit(65)
    if count == 1 and mode == "timeout-after":
        os.close(1)
        os.close(2)
        time.sleep(10)


def initial(category, field):
    choices = {"choice." + c: {"description": "Choice " + c.upper(), "available": True}
               for c in "abc"}
    ports = [{"name": name, "description": meta["description"], "availability": "available"}
             for name, meta in choices.items()]
    data = {"cards": [{"index": 40, "name": "card.alpha", "description": "Primary card",
                        "active_profile": "choice.a", "profiles": choices}],
            "sinks": [], "sources": [], "sink-inputs": [], "source-outputs": [],
            "writes": 0, "reads": 0, "afterReads": 0, "category": category, "field": field}
    for key, index, raw, label in (("sinks", 10, "sink.alpha", "Output"),
                                   ("sources", 20, "source.alpha", "Input")):
        data[key] = [{"index": index, "name": raw, "description": label,
                      "volume": {"mono": {"value": 32768}}, "mute": False,
                      "active_port": "choice.a", "ports": ports}]
    data["old"] = data[category][0][field]
    if field == "volume":
        data["next"] = {"mono": {"value": 39322}}
        data["third"] = {"mono": {"value": 24248}}
    elif field == "mute":
        data["next"] = True
        data["third"] = False  # no third Boolean value; that case is excluded
    else:
        data["next"], data["third"] = "choice.b", "choice.c"
    return data


def invoke(binary, args, env):
    libc = ctypes.CDLL(None, use_errno=True)
    parent = os.getpid()
    def prepare():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0 or os.getppid() != parent:
            os._exit(126)
    process = subprocess.Popen([str(binary), "audio", *args], cwd="/", env=env,
                               stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, start_new_session=True, preexec_fn=prepare)
    output = {process.stdout.fileno(): bytearray(), process.stderr.fileno(): bytearray()}
    deadline = time.monotonic() + 8
    def kill():
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    try:
        with selectors.DefaultSelector() as selector:
            for stream in (process.stdout, process.stderr):
                os.set_blocking(stream.fileno(), False)
                selector.register(stream, selectors.EVENT_READ)
            while selector.get_map() or process.poll() is None:
                assert time.monotonic() < deadline, "Audio fixture command exceeded deadline"
                if process.poll() is not None:
                    kill()
                for key, _ in selector.select(0.02):
                    chunk = os.read(key.fd, 4096)
                    if not chunk:
                        selector.unregister(key.fileobj)
                    else:
                        output[key.fd].extend(chunk)
                        assert sum(map(len, output.values())) <= 65536, "Audio diagnostic limit"
        return process.returncode, *(bytes(part).decode("utf-8") for part in output.values())
    finally:
        kill()
        process.wait(timeout=2)
        process.stdout.close()
        process.stderr.close()


def capture_contract(binary, env, root):
    # Adopt/reap only this exact fixture descendant; never scan or kill by name.
    assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0
    write_json(root / "state.json", initial("sinks", "volume"))
    child = group = None
    reaped = False
    try:
        code, out, err = invoke(binary, ["inventory", "--format", "json"],
                                {**env, "SYNAPSE_AUDIO_UNITS_MODE": "leader-descendant"})
        marker = root / "descendant.json"
        assert marker.is_file() and marker.stat().st_size < 128
        owned = json.loads(marker.read_bytes())
        child, group = owned["pid"], owned["group"]
        assert child > 1 and group > 1 and child != group
        assert code == 0 and not err and json.loads(out)["available"]
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            waited, status = os.waitpid(child, os.WNOHANG)
            if waited == child:
                reaped = True
                assert os.WIFSIGNALED(status) and os.WTERMSIG(status) == signal.SIGKILL
                break
            time.sleep(.005)
        assert reaped, "successful C capture leader left its owned descendant alive"
    finally:
        if child is None and (root / "descendant.json").is_file():
            owned = json.loads((root / "descendant.json").read_bytes())
            child, group = owned["pid"], owned["group"]
        if child is not None and not reaped:
            assert child > 1 and group > 1 and os.getpgid(child) == group
            os.killpg(group, signal.SIGKILL)
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                if os.waitpid(child, os.WNOHANG)[0] == child:
                    reaped = True
                    break
                time.sleep(.005)
            assert reaped, "owned descendant cleanup failed"
    print("audio-capture: PASS successful-leader whole-group termination and exact reaping")


def main():
    contract_only = len(sys.argv) == 3 and sys.argv[1] == "--capture-contract"
    if contract_only:
        del sys.argv[1]
    assert 2 <= len(sys.argv) <= 4, "binary [exclusive-output-directory [expected-corpus]]"
    binary = Path(sys.argv[1])
    metadata = binary.lstat()
    assert binary.is_absolute() and binary.name == "synapse-settings-test"
    assert stat.S_ISREG(metadata.st_mode) and 0 < metadata.st_size < 8 * 1024 * 1024
    assert os.access(binary, os.X_OK)
    # A production binary is refused before any Audio inventory call.
    assert b"SYNAPSE_PACTL\0" in binary.read_bytes()
    destination = Path(sys.argv[2]) if len(sys.argv) >= 3 and sys.argv[2] else None
    if destination:
        assert destination.is_absolute()
        destination.mkdir(mode=0o700)  # existing output is never overwritten
    os.umask(0o077)
    rows = []
    with tempfile.TemporaryDirectory(prefix="au25-", dir="/tmp") as temporary:
        root = Path(temporary)
        for name in ("home", "config", "state", "runtime", "cache", "tmp"):
            (root / name).mkdir(mode=0o700)
        fake = root / "pactl-units-fixture"
        fake.write_bytes(Path(__file__).read_bytes())
        fake.chmod(0o700)
        env = {"PATH": "/usr/bin:/bin", "LC_ALL": "C", "LANG": "C", "TZ": "UTC",
               "HOME": str(root / "home"), "XDG_CONFIG_HOME": str(root / "config"),
               "XDG_STATE_HOME": str(root / "state"), "XDG_RUNTIME_DIR": str(root / "runtime"),
               "XDG_CACHE_HOME": str(root / "cache"), "TMPDIR": str(root / "tmp"),
               "SOURCE_DATE_EPOCH": "1", "SYNAPSE_PACTL": str(fake),
               "SYNAPSE_AUDIO_UNITS_STATE": str(root),
               "ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "halt_on_error=1"}
        if "LD_LIBRARY_PATH" in os.environ:
            env["LD_LIBRARY_PATH"] = os.environ["LD_LIBRARY_PATH"]
        def call(name, args, status, mode="success"):
            (root / "trace").write_text("")
            code, out, err = invoke(binary, args, {**env, "SYNAPSE_AUDIO_UNITS_MODE": mode})
            trace = [json.loads(line) for line in (root / "trace").read_text().splitlines()]
            row = {"case": name, "argv": args, "status": code, "stdout": out,
                   "stderr": err, "trace": trace}
            rows.append(row)
            if destination:
                with (destination / (f"{len(rows):03d}.json")).open("x") as stream:
                    json.dump(row, stream, indent=2)
                    stream.write("\n")
            assert code == status, (name, row)
            if status != 2:
                assert not err, (name, err)
            return json.loads(out) if out.startswith("{") else None, trace
        capture_contract(binary, env, root)
        if contract_only:
            return
        families = [("sinks", "volume", "volume", "output"),
                    ("sources", "volume", "volume", "input"),
                    ("sinks", "mute", "mute", "output"),
                    ("sources", "mute", "mute", "input"),
                    ("cards", "active_profile", "profile", "card"),
                    ("sinks", "active_port", "port", "output"),
                    ("sources", "active_port", "port", "input")]
        modes = ("success", "same", "fail", "no-mutate", "fail-after", "timeout-after",
                 "postflight-unavailable", "identity", "vanish", "cohort", "original",
                 "drift", "external", "intervene", "rollback-fail", "ack")
        for category, field, kind, direction in families:
            selection = kind in ("profile", "port")
            for mode in modes:
                if kind == "mute" and mode == "intervene":
                    continue
                name = f"{direction}-{kind}-{mode}"
                write_json(root / "state.json", initial(category, field))
                inventory, _ = call(name + "-inventory", ["profile-port-inventory" if selection else "inventory", "--format", "json"], 0)
                target = (next(item for item in inventory["endpoints"] if item["direction"] == direction)
                          if kind == "port" else inventory[{"sinks": "outputs", "sources": "inputs", "cards": "cards"}[category]][0])
                target_args = ["--card" if kind == "profile" else "--device", target["id"]] if selection else ["--target", target["id"]]
                if kind == "port":
                    target_args += ["--direction", direction]
                flag = "--" + ("percent" if kind == "volume" else "muted" if kind == "mute" else kind)
                if selection:
                    options = target["profiles" if kind == "profile" else "ports"]
                    value = next(option["id"] for option in options if option["label"] == ("Choice A" if mode == "same" else "Choice B"))
                else:
                    value = ("50" if mode == "same" else "60") if kind == "volume" else ("false" if mode == "same" else "true")
                args = ["plan-" + kind, *target_args, flag, value, "--format", "json"]
                plan, trace = call(name + "-plan", args, 0)
                assert plan["status"] == "Planned" and not any(command[0].startswith("set-") for command in trace)
                original = plan["originalSelection"] if selection else str(plan["originalValue"]).lower()
                if mode == "original":
                    original = value
                cohort = plan["cohort"]
                if mode == "cohort":
                    cohort = cohort[:-16] + "0" * 16
                data = json.loads((root / "state.json").read_bytes())
                data["reads"] = data["afterReads"] = 0
                write_json(root / "state.json", data)
                apply = ["set-" + kind, *target_args, "--from-" + flag[2:], original,
                         flag, value, "--cohort", cohort, "--ack",
                         "wrong" if mode == "ack" else plan["requiresAcknowledgement"], "--format", "json"]
                receipt, trace = call(name + "-apply", apply, 2 if mode == "ack" else 0 if mode in ("success", "same") else 1, mode)
                mutations = [command for command in trace if command[0].startswith("set-")]
                if mode == "ack":
                    assert not trace
                    continue
                expected = "Applied" if mode == "success" else "AlreadySet" if mode == "same" else "Refused" if mode in ("cohort", "original", "drift") else "Failed"
                assert receipt["status"] == expected and receipt["verified"] == (mode in ("success", "same")), (name, receipt)
                rollback = mode in ("fail-after", "timeout-after", "rollback-fail")
                assert receipt["rollbackAttempted"] == rollback
                assert receipt["rollbackVerified"] == (rollback and mode != "rollback-fail")
                count = 0 if mode in ("same", "cohort", "original", "drift") else 2 if rollback else 1
                assert len(mutations) == count, (name, mutations)
                assert receipt["mutationAttempted"] == (count > 0)
                assert receipt["stateAuthority"] == "pipewire-pulse-model"
                assert not receipt["playbackStarted"] and not receipt["captureStarted"]
        # Parser refusals never call the backend, irrespective of session language.
        for language in ("C", "it_IT.UTF-8", "ar.UTF-8"):
            env["LC_ALL"] = env["LANG"] = language
            for command in ("plan-volume", "set-volume", "plan-mute", "set-mute", "plan-profile", "set-profile", "plan-port", "set-port"):
                _, trace = call(language + "-" + command, [command, "--unknown"], 2)
                assert trace == []
    corpus = {"schema": "synapse.settings.test-audio-units/v1", "cases": rows}
    if destination:
        with (destination / "corpus.json").open("x") as stream:
            json.dump(corpus, stream, indent=2)
            stream.write("\n")
    if len(sys.argv) == 4:
        assert corpus == json.loads(Path(sys.argv[3]).read_bytes()), "Audio original behavior differs"
    print(f"audio-units: PASS observations={len(rows)} transactions=110 exact-command-traces offline")


if __name__ == "__main__":
    if Path(sys.argv[0]).name == "pactl-units-fixture":
        fixture()
    else:
        main()
