"""Host-side structural validation for the hand-authored campaign maps."""
from pathlib import Path
import unittest
import re

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = [
    "The Entrance", "Forgotten Halls", "Burial Grounds", "Bone Chambers", "Bone King's Throne",
    "Mine Entrance", "Crystal Caverns", "Collapsing Tunnels", "Deep Shafts", "Broodmother's Nest",
    "Ashen Path", "Burning Halls", "Magma Bridge", "Furnace Chambers", "Inferno's Arena",
    "Forgotten Gate", "Shattered Reality", "Lost Kingdom", "Abyssal Core", "The Forgotten One",
]


class CampaignTests(unittest.TestCase):
    def test_twenty_distinct_maps(self):
        maps = []
        embedded = re.findall(
            r'\{"([^"\r\n]+)",(\d+),(\d+),(\d+),\{([^}]*)\}\}',
            (ROOT / "src" / "level_data.c").read_text(encoding="utf-8"),
        )
        self.assertEqual(len(embedded), 20)
        for world in range(1, 5):
            for level in range((world - 1) * 5 + 1, world * 5 + 1):
                path = ROOT / "levels" / f"world{world}" / f"level{level:02}.map"
                rows = path.read_text(encoding="utf-8").splitlines()
                name, source_world, spawn_x, spawn_y, raw_rows = embedded[level - 1]
                source_rows = [r[:44].ljust(44, ".") for r in re.findall(r'"([.#=\^crkdS]+)"', raw_rows)]
                self.assertEqual(name, EXPECTED[level - 1])
                self.assertEqual(int(source_world), world - 1)
                self.assertEqual((int(spawn_x), int(spawn_y)), (1, 12))
                self.assertEqual(source_rows, rows, f"embedded map differs: level {level}")
                self.assertEqual(len(rows), 15, path)
                self.assertTrue(all(len(row) == 44 for row in rows), path)
                self.assertEqual(sum(row.count("S") for row in rows), 1, path)
                self.assertGreaterEqual(sum(row.count("d") for row in rows), 1, path)
                self.assertGreaterEqual(sum(row.count("#") + row.count("=") for row in rows), 45, path)
                self.assertGreaterEqual(sum(row.count("c") + row.count("k") for row in rows), 1, path)
                self.assertLessEqual(sum(row.count("c") for row in rows), 4, path)
                self.assertGreaterEqual(sum(row.count("^") for row in rows), 1, path)
                allowed = set(".#=^crkdS")
                self.assertTrue(all(set(row) <= allowed for row in rows), path)
                sx, sy = int(spawn_x), int(spawn_y)
                self.assertEqual(rows[sy][sx], "S", path)
                self.assertIn(rows[sy + 1][sx], "#=", path)
                ey = next(i for i, row in enumerate(rows) if "d" in row)
                ex = rows[ey].index("d")
                self.assertIn(rows[ey + 1][ex], "#=", path)
                maps.append(tuple(rows))
        self.assertEqual(len(maps), 20)
        self.assertEqual(len(set(maps)), 20)

    def test_guardian_levels_have_three_runes(self):
        for level in (5, 10, 15, 20):
            rows = (ROOT / "levels" / f"world{(level-1)//5+1}" / f"level{level:02}.map").read_text().splitlines()
            self.assertGreaterEqual(sum(row.count("r") for row in rows), 3)


if __name__ == "__main__":
    unittest.main()
