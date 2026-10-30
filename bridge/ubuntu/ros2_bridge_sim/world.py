"""A small static 2D world: obstacle rectangles, OccupancyGrid, ray-cast lidar.

Coordinates are metres in the ``map`` frame.  The occupancy grid is rendered
row major with x varying fastest and the origin at the lower left corner, as
required by ``nav_msgs/OccupancyGrid``.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass

#: OccupancyGrid cell values.
FREE = 0
OCCUPIED = 100
UNKNOWN = -1

#: Default lidar geometry.
DEFAULT_BEAMS = 360
DEFAULT_RANGE_MIN = 0.15
DEFAULT_RANGE_MAX = 12.0


@dataclass(frozen=True)
class Region:
    """An axis aligned rectangle in the map frame."""

    x0: float
    y0: float
    x1: float
    y1: float
    value: int = OCCUPIED

    def contains(self, x: float, y: float) -> bool:
        return self.x0 <= x <= self.x1 and self.y0 <= y <= self.y1


def ray_box(
    ox: float, oy: float, dx: float, dy: float, x0: float, y0: float, x1: float, y1: float
) -> float | None:
    """Distance along the unit ray ``(dx, dy)`` to the rectangle, or None.

    A ray starting inside the rectangle yields ``0.0`` (the caller compares
    against ``range_min`` and drops it).
    """
    t_min, t_max = 0.0, math.inf
    for origin, direction, low, high in ((ox, dx, x0, x1), (oy, dy, y0, y1)):
        if direction == 0.0:
            if origin < low or origin > high:
                return None
            continue
        t1 = (low - origin) / direction
        t2 = (high - origin) / direction
        if t1 > t2:
            t1, t2 = t2, t1
        t_min = max(t_min, t1)
        t_max = min(t_max, t2)
        if t_min > t_max:
            return None
    return t_min


class World:
    """Static obstacle map plus a 2D lidar model."""

    def __init__(
        self,
        width: float = 24.0,
        height: float = 24.0,
        resolution: float = 0.05,
        origin: tuple[float, float] = (-12.0, -12.0),
    ) -> None:
        self.resolution = resolution
        self.origin_x, self.origin_y = origin
        self.cells_x = max(1, round(width / resolution))
        self.cells_y = max(1, round(height / resolution))
        self.regions: list[Region] = []
        self._data: list[int] | None = None

    # -- geometry ----------------------------------------------------------

    @property
    def width(self) -> float:
        return self.cells_x * self.resolution

    @property
    def height(self) -> float:
        return self.cells_y * self.resolution

    @property
    def bounds(self) -> tuple[float, float, float, float]:
        return (self.origin_x, self.origin_y, self.origin_x + self.width, self.origin_y + self.height)

    def clear(self) -> None:
        self.regions.clear()
        self._data = None

    def add_region(self, x: float, y: float, w: float, h: float, value: int = OCCUPIED) -> Region:
        """Add a rectangle centred on ``(x, y)`` with size ``w`` x ``h``."""
        region = Region(x - w / 2.0, y - h / 2.0, x + w / 2.0, y + h / 2.0, value)
        self.regions.append(region)
        self._data = None
        return region

    def add_obstacle(self, x: float, y: float, w: float, h: float) -> Region:
        """Add an occupied rectangle centred on ``(x, y)``."""
        return self.add_region(x, y, w, h, OCCUPIED)

    def add_boundary_walls(self, thickness: float = 0.2) -> None:
        """Frame the world with four walls so the lidar always sees something."""
        x0, y0, x1, y1 = self.bounds
        half_t = thickness / 2.0
        self.add_obstacle((x0 + x1) / 2.0, y0 + half_t, x1 - x0, thickness)
        self.add_obstacle((x0 + x1) / 2.0, y1 - half_t, x1 - x0, thickness)
        self.add_obstacle(x0 + half_t, (y0 + y1) / 2.0, thickness, y1 - y0)
        self.add_obstacle(x1 - half_t, (y0 + y1) / 2.0, thickness, y1 - y0)

    def populate(self, seed: int | None = None, count: int = 6, keep_clear_radius: float = 2.5) -> None:
        """Add boundary walls plus a few seeded interior obstacles."""
        rng = random.Random(seed)
        self.add_boundary_walls()
        x0, y0, x1, y1 = self.bounds
        placed, attempts = 0, 0
        while placed < count and attempts < count * 50:
            attempts += 1
            w = rng.uniform(0.6, 2.4)
            h = rng.uniform(0.6, 2.4)
            x = rng.uniform(x0 + 2.0, x1 - 2.0)
            y = rng.uniform(y0 + 2.0, y1 - 2.0)
            if abs(x) < keep_clear_radius and abs(y) < keep_clear_radius:
                continue  # keep the start pose and its surroundings clear
            self.add_obstacle(x, y, w, h)
            placed += 1

    # -- occupancy grid ----------------------------------------------------

    def cell_index(self, x: float, y: float) -> tuple[int, int]:
        """Grid indices of the cell containing the point ``(x, y)``."""
        return (
            int(math.floor((x - self.origin_x) / self.resolution)),
            int(math.floor((y - self.origin_y) / self.resolution)),
        )

    def occupancy_value(self, x: float, y: float) -> int:
        """Occupancy value of the cell containing ``(x, y)``."""
        ix, iy = self.cell_index(x, y)
        if not (0 <= ix < self.cells_x and 0 <= iy < self.cells_y):
            return UNKNOWN
        cx = self.origin_x + (ix + 0.5) * self.resolution
        cy = self.origin_y + (iy + 0.5) * self.resolution
        value = FREE
        for region in self.regions:
            if region.contains(cx, cy):
                value = region.value
        return value

    def occupancy_data(self) -> list[int]:
        """Signed int8 occupancy cells, row major with x varying fastest."""
        if self._data is None:
            raw = bytearray(self.cells_x * self.cells_y)
            for region in self.regions:
                ix0 = max(0, int(math.floor((region.x0 - self.origin_x) / self.resolution)))
                ix1 = min(self.cells_x - 1, int(math.ceil((region.x1 - self.origin_x) / self.resolution)) - 1)
                iy0 = max(0, int(math.floor((region.y0 - self.origin_y) / self.resolution)))
                iy1 = min(self.cells_y - 1, int(math.ceil((region.y1 - self.origin_y) / self.resolution)) - 1)
                value = region.value & 0xFF
                for iy in range(iy0, iy1 + 1):
                    row = iy * self.cells_x
                    for ix in range(ix0, ix1 + 1):
                        raw[row + ix] = value
            self._data = list(memoryview(raw).cast("b"))
        return self._data

    # -- lidar -------------------------------------------------------------

    def scan(
        self,
        x: float,
        y: float,
        theta: float = 0.0,
        *,
        beams: int = DEFAULT_BEAMS,
        angle_min: float = -math.pi,
        range_min: float = DEFAULT_RANGE_MIN,
        range_max: float = DEFAULT_RANGE_MAX,
    ) -> list[float]:
        """Ray-cast ``beams`` rays over a full turn and return the ranges.

        ``inf`` marks a beam that saw nothing within ``range_max``.
        """
        increment = 2.0 * math.pi / beams
        boxes = [(r.x0, r.y0, r.x1, r.y1) for r in self.regions]
        ranges: list[float] = []
        for index in range(beams):
            angle = theta + angle_min + index * increment
            dx, dy = math.cos(angle), math.sin(angle)
            best = math.inf
            for x0, y0, x1, y1 in boxes:
                hit = ray_box(x, y, dx, dy, x0, y0, x1, y1)
                if hit is None or hit < range_min or hit > range_max:
                    continue
                if hit < best:
                    best = hit
            ranges.append(best)
        return ranges

    @staticmethod
    def scan_geometry(beams: int = DEFAULT_BEAMS) -> tuple[float, float, float]:
        """``(angle_min, angle_max, angle_increment)`` for a beam count."""
        increment = 2.0 * math.pi / beams
        return -math.pi, -math.pi + (beams - 1) * increment, increment