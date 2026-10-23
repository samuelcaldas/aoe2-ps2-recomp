import unittest

from tests.test_kcet_resolver_elf import PF_RX, build_elf, code
from tools.boundary_inventory import (
    GCC, MWCC, OTHER, SYSCALL, classify_function, clusters, find_boundary)
from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.functions import FunctionMap
from tools.kcet_resolver.mips import JR_RA, addiu, jal

BASE = 0x100000
SQ_RA = (0x1F << 26) | (29 << 21) | (31 << 16) | 0x20   # sq ra,0x20(sp)
SD_RA = (0x3F << 26) | (29 << 21) | (31 << 16) | 0x10   # sd ra,0x10(sp)
SYSCALL_WORD = 0x0000000C


def program(*bodies):
    rows, words, address = [], [], BASE
    for body in bodies:
        rows.append((address, address + 4 * len(body), f"FUN_{address:08x}"))
        words += body
        address += 4 * len(body)
    data = code(*words)
    return ElfImage(build_elf([(BASE, data, len(data), PF_RX)])), FunctionMap.from_rows(rows), rows


class TestClassify(unittest.TestCase):
    def test_frame_save_decides_the_compiler(self):
        self.assertEqual(classify_function([addiu(29, 29, 0xFFD0), SQ_RA, JR_RA, 0]), MWCC)
        self.assertEqual(classify_function([addiu(29, 29, 0xFFD0), SD_RA, JR_RA, 0]), GCC)
        self.assertEqual(classify_function([JR_RA, 0]), OTHER)

    def test_syscall_stub_is_its_own_class(self):
        self.assertEqual(classify_function([addiu(3, 0, 0x40), SYSCALL_WORD, JR_RA, 0]), SYSCALL)


class TestBoundary(unittest.TestCase):
    def test_edges_run_from_engine_code_to_compat_code(self):
        engine = [SQ_RA, 0, 0, 0]
        compat = [SD_RA, JR_RA, 0]
        engine[1] = jal(BASE + 16)
        image, functions, rows = program(engine, compat)
        edges = find_boundary(image, functions, rows)
        self.assertEqual({(e.caller, e.callee, e.callee_class) for e in edges}, {(BASE, BASE + 16, GCC)})

    def test_engine_to_engine_and_compat_to_compat_are_not_boundary(self):
        engine_a = [SQ_RA, jal(BASE + 16), 0, 0]
        engine_b = [SQ_RA, JR_RA, 0, 0]
        compat_a = [SD_RA, jal(BASE + 44), 0]
        compat_b = [SD_RA, JR_RA, 0]
        image, functions, rows = program(engine_a, engine_b, compat_a, compat_b)
        self.assertEqual(find_boundary(image, functions, rows), [])

    def test_clusters_group_nearby_targets(self):
        self.assertEqual(clusters([0x1000, 0x1010, 0x1020, 0x9000, 0x9008], gap=0x400),
                         [(0x1000, 0x1020), (0x9000, 0x9008)])


if __name__ == "__main__":
    unittest.main()
