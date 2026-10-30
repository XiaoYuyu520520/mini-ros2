#!/usr/bin/env bash
# racecar mapping: slam_toolbox on config/slam_toolbox.yaml - one line around tools/racecar_ctl.py, which is the same code the
# Windows twins run (gmapping.bat), so the robot and this desk behave alike.
# See tools/racecar_ctl.py for the options; everything is passed through.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${PYTHON:-python3}" "$HERE/tools/racecar_ctl.py" slam "$@"
