"""``geometry_msgs.msg.Twist``: the name a ROS2 script uses, for generated classes.

The message classes are generated into ``pymessages/`` under their full package
path - ``pymessages.geometry_msgs.msg.twist`` - because that is what the
generator emits and what the interop tests import. A ROS2 Python script says
``from geometry_msgs.msg import Twist``, and the point of this package is that
such a script runs unchanged, so the unprefixed name has to resolve to the very
same class object: two classes that are equal field for field but distinct
objects would break the ``isinstance`` checks the generated messages' own
serialization and the ``f_msg(...)`` field descriptors rely on.

One finder rather than forty generated stub packages, and lazily: the set of
message packages is exactly the set of directories under ``pymessages/``, and
importing all six hundred generated modules to run a script that uses three of
them would be most of a second spent for nothing.

The rule for the two kinds of name:

* a **package** (``geometry_msgs``, ``geometry_msgs.msg``) gets a fresh module
  whose contents are the public names of the leaf modules next to it - which is
  what a real ROS2 package's ``__init__.py`` does, ``from .twist import
  Twist`` - so ``from geometry_msgs.msg import Twist`` finds ``Twist``.
* a **leaf** (``geometry_msgs.msg.twist``) is the generated module *itself*,
  registered under its ROS2 name as well, so there is exactly one class object.
"""
from __future__ import annotations

import importlib
import importlib.abc
import importlib.util
import sys
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent      # .../ros2_shim/python
PREFIX = "pymessages"

_installed = []


def message_packages():
    """The top-level names the generated tree can answer for."""
    base = ROOT / PREFIX
    if not base.is_dir():
        return []
    return sorted(entry.name for entry in base.iterdir()
                  if entry.is_dir() and (entry / "__init__.py").is_file())


def _real_name(fullname: str):
    """``geometry_msgs.msg.pose_stamped`` -> ``pymessages.geometry_msgs...``.

    None when this is not a name the generated tree has: the finder is at the
    end of ``sys.meta_path`` but it still has to answer for every import in the
    process, including ``rclpy`` and ``os``.
    """
    head = fullname.split(".", 1)[0]
    if head == PREFIX or head not in _message_package_names:
        return None
    return PREFIX + "." + fullname


_message_package_names = []


class _AliasLoader(importlib.abc.Loader):
    """Hands back an already-imported module under its ROS2 name."""

    def __init__(self, real_name: str):
        self.real_name = real_name

    def create_module(self, spec):
        return importlib.import_module(self.real_name)

    def exec_module(self, module):
        pass


class _PackageLoader(importlib.abc.Loader):
    """A fresh package module, populated the way a generated ``__init__`` would.

    ``__path__`` stays empty on purpose. The subpackages and leaf modules under
    it are resolved by the finder above, which aliases them onto the generated
    objects; a non-empty ``__path__`` would let the ordinary path finder
    *execute* ``twist.py`` a second time under the alias name, and the second
    ``Twist`` class is the thing this module exists to avoid.
    """

    def __init__(self, real_name: str, directory: Path):
        self.real_name = real_name
        self.directory = directory

    def create_module(self, spec):
        module = types.ModuleType(spec.name)
        module.__path__ = []
        return module

    def exec_module(self, module):
        for entry in sorted(self.directory.iterdir()):
            if entry.suffix != ".py" or entry.stem == "__init__":
                continue
            child = importlib.import_module(self.real_name + "." + entry.stem)
            for name in dir(child):
                if not name.startswith("_"):
                    setattr(module, name, getattr(child, name))


class _Finder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        real = _real_name(fullname)
        if real is None:
            return None
        base = ROOT / PREFIX / fullname.replace(".", "/")
        if base.is_dir() and (base / "__init__.py").is_file():
            return importlib.util.spec_from_loader(fullname, _PackageLoader(real, base),
                                                  is_package=True)
        if base.with_suffix(".py").is_file():
            return importlib.util.spec_from_loader(fullname, _AliasLoader(real))
        return None


def install() -> bool:
    """Make the ROS2 message names importable. Idempotent.

    Called from ``rclpy``'s import (every one of these scripts imports
    ``rclpy`` before it names a message) and by the runner that starts a script,
    which is the one place that can promise it happens before the script's own
    first line runs.
    """
    if _installed:
        return True
    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    names = message_packages()
    if not names:
        return False
    _message_package_names.extend(names)
    sys.meta_path.append(_Finder())
    _installed.append(True)
    return True
