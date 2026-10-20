"""Declared file layouts of user-owned HD-edition installs."""

from dataclasses import dataclass


@dataclass(frozen=True)
class EditionProfile:
    """Sentinel files that identify an edition, and the folder or file behind each category."""

    key: str
    sentinel_paths: tuple[str, ...]
    category_paths: tuple[tuple[str, str], ...]

    def category_names(self) -> tuple[str, ...]:
        return tuple(name for name, _ in self.category_paths)

    def path_for(self, category: str) -> str:
        return dict(self.category_paths)[category]


HD_2013 = EditionProfile(
    key="HD_2013",
    sentinel_paths=(
        "AoK HD.exe",
        "resources/_common/dat/empires2_x1_p1.dat",
    ),
    category_paths=(
        ("interface", "resources/_common/drs/interface"),
        ("graphics", "resources/_common/drs/graphics"),
        ("terrain", "resources/_common/terrain/textures"),
        ("gamedata", "resources/_common/drs/gamedata_x1"),
        ("datfile", "resources/_common/dat/empires2_x1_p1.dat"),
    ),
)
