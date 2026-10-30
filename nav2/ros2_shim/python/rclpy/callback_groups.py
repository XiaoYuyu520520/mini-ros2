"""Callback groups, which the shim does not schedule separately.

There is one thread and one defined callback order here - see
:mod:`ros2py.executor` - so a group has nothing left to control. The classes
exist so that a script which names one imports, and so that
``create_subscription(..., callback_group=...)`` accepts the argument.
"""
from __future__ import annotations


class CallbackGroup:
    """A group, and the entities in it - which nothing here iterates."""

    def __init__(self, **kwargs):
        self._entities = []

    def add_entity(self, entity) -> None:
        self._entities.append(entity)

    def remove_entity(self, entity) -> None:
        if entity in self._entities:
            self._entities.remove(entity)

    def has_entity(self, entity) -> bool:
        return entity in self._entities


class MutuallyExclusiveCallbackGroup(CallbackGroup):
    """Two callbacks of one group never overlap - which is trivially true."""


class ReentrantCallbackGroup(CallbackGroup):
    """Callbacks of one group may overlap - which nothing here does."""
