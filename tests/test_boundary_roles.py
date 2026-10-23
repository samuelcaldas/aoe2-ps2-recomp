import unittest

from tests.test_boundary_inventory import BASE, SD_RA, SQ_RA, SYSCALL_WORD, program
from tools.boundary_inventory import find_boundary
from tools.boundary_roles import ROLE_HARDWARE, ROLE_KERNEL, ROLE_UNKNOWN, assign_roles
from tools.kcet_resolver.mips import JR_RA, addiu, jal, lui, ori


def roles_for(*bodies):
    image, functions, rows = program(*bodies)
    edges = find_boundary(image, functions, rows)
    return assign_roles(image, functions, rows, edges)


class TestAssignRoles(unittest.TestCase):
    def test_service_id_in_the_callee_names_its_role(self):
        engine = [SQ_RA, jal(BASE + 16), 0, 0]
        compat = [SD_RA, lui(2, 0x8000), ori(5, 2, 0x0592), JR_RA, 0]
        self.assertEqual(roles_for(engine, compat), {BASE + 16: "cdvd"})

    def test_service_id_found_in_a_function_the_callee_calls(self):
        engine = [SQ_RA, jal(BASE + 16), 0, 0]
        wrapper = [SD_RA, jal(BASE + 16 + 24), 0, JR_RA, 0, 0]
        inner = [SD_RA, lui(2, 0x8000), ori(5, 2, 0x0100), JR_RA, 0]
        self.assertEqual(roles_for(engine, wrapper, inner), {BASE + 16: "pad"})

    def test_unmapped_service_id_is_reported_as_a_number_not_guessed(self):
        engine = [SQ_RA, jal(BASE + 16), 0, 0]
        compat = [SD_RA, lui(2, 0x8000), ori(5, 2, 0x0777), JR_RA, 0]
        self.assertEqual(roles_for(engine, compat), {BASE + 16: "sid_0x80000777"})

    def test_hardware_register_base_marks_a_hardware_role(self):
        engine = [SQ_RA, jal(BASE + 16), 0, 0]
        compat = [SD_RA, lui(2, 0x1200), JR_RA, 0]
        self.assertEqual(roles_for(engine, compat), {BASE + 16: ROLE_HARDWARE})

    def test_sif_command_ids_are_transport_not_a_service(self):
        engine = [SQ_RA, jal(BASE + 16), 0, 0]
        compat = [SD_RA, lui(2, 0x8000), ori(5, 2, 0x000A), JR_RA, 0]
        self.assertEqual(roles_for(engine, compat), {BASE + 16: "sif_transport"})

    def test_a_service_wins_over_transport_ids_in_the_same_closure(self):
        engine = [SQ_RA, jal(BASE + 16), 0, 0]
        compat = [SD_RA, lui(2, 0x8000), ori(5, 2, 0x000A), lui(3, 0x8000), ori(6, 3, 0x0400), JR_RA, 0]
        self.assertEqual(roles_for(engine, compat), {BASE + 16: "memory_card"})

    def test_syscall_stub_is_kernel(self):
        engine = [SQ_RA, jal(BASE + 16), 0, 0]
        stub = [addiu(3, 0, 0x40), SYSCALL_WORD, JR_RA, 0]
        self.assertEqual(roles_for(engine, stub), {BASE + 16: ROLE_KERNEL})

    def test_no_evidence_is_unknown(self):
        engine = [SQ_RA, jal(BASE + 16), 0, 0]
        compat = [SD_RA, JR_RA, 0]
        self.assertEqual(roles_for(engine, compat), {BASE + 16: ROLE_UNKNOWN})


if __name__ == "__main__":
    unittest.main()
