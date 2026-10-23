"""
dgx_ge_now_events of project_include.cmake: the depth of the queue of events at once for the whole build, 1 to 255,
said once; run by cmake -P over the build's properties as ESP-IDF keeps them (stand-ins of idf_build_get_property and
idf_build_set_property: the property COMPILE_DEFINITIONS of the build, which reaches every component).
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
INCLUDE = os.path.join(HERE, "..", "..", "project_include.cmake")

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


def configure(*calls):
    """The calls made in a script with the engine's project_include.cmake: (exit code, the definitions, what it said)."""
    with tempfile.TemporaryDirectory() as tmp:
        script = os.path.join(tmp, "t.cmake")
        with open(script, "w") as f:
            f.write(
                "function(idf_build_get_property var prop)\n"
                "    get_property(v GLOBAL PROPERTY IDF_BUILD_${prop})\n"
                "    set(${var} \"${v}\" PARENT_SCOPE)\n"
                "endfunction()\n"
                "function(idf_build_set_property prop value)\n"
                "    set_property(GLOBAL APPEND PROPERTY IDF_BUILD_${prop} \"${value}\")\n"
                "endfunction()\n"
                f"include(\"{INCLUDE}\")\n"
                + "".join(f"{c}\n" for c in calls)
                + "get_property(v GLOBAL PROPERTY IDF_BUILD_COMPILE_DEFINITIONS)\n"
                "message(\"DEFS=${v}\")\n"
            )
        r = subprocess.run(["cmake", "-P", script], capture_output=True, text=True)
        defs = [line[5:] for line in r.stderr.splitlines() if line.startswith("DEFS=")]
        return r.returncode, defs[0].split(";") if defs and defs[0] else [], r.stderr


if not shutil.which("cmake"):
    print("test_now_events.py: skipped (no cmake)")
    sys.exit(0)

for n in (1, 4, 8, 255):
    rc, defs, said = configure(f"dgx_ge_now_events({n})")
    check(rc == 0 and defs == [f"DGX_GE_ANIM_NOW_EVENTS={n}"], f"dgx_ge_now_events({n}): {rc} {defs} {said[-300:]}")

# beside the params of the pack, in either order: each its own definition
rc, defs, said = configure("idf_build_set_property(COMPILE_DEFINITIONS \"DGX_GE_ANIM_PARAMS=8\" APPEND)", "dgx_ge_now_events(4)")
check(rc == 0 and defs == ["DGX_GE_ANIM_PARAMS=8", "DGX_GE_ANIM_NOW_EVENTS=4"], f"with the params: {rc} {defs}")

for bad in ("0", "256", "x", "4.5", "-1", "04x"):
    rc, defs, said = configure(f"dgx_ge_now_events({bad})")
    check(rc != 0 and "1 to 255" in said and "DGX_GE_ANIM_NOW_EVENTS" not in " ".join(defs), f"dgx_ge_now_events({bad}): {rc} {said[-300:]}")

# one value for the whole build: said twice is an error, not a second definition
rc, defs, said = configure("dgx_ge_now_events(4)", "dgx_ge_now_events(8)")
check(rc != 0 and "said already" in said, f"said twice: {rc} {said[-300:]}")

if failed:
    print("\n".join(failed))
    sys.exit(1)
print("test_now_events.py: ok")
