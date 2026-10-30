#!/usr/bin/env bash
# racecar navigation: the nav2 stack on config/nav.yaml - one line around tools/racecar_ctl.py, which is the same code the
# Windows twins run (nav.bat), so the robot and this desk behave alike.
# See tools/racecar_ctl.py for the options; everything is passed through.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${PYTHON:-python3}" "$HERE/tools/racecar_ctl.py" nav "$@"
