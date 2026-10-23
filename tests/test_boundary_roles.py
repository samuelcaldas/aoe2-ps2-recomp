import unittest

from tests.test_boundary_inventory import BASE, SD_RA, SQ_RA, SYSCALL_WORD, program
from tools.boundary_inventory import find_boundary
from tools.boundary_roles import ROLE_HARDWARE, ROLE_KERNEL, ROLE_UNKNOWN, assign_roles, find_bind_function, service_bind_sites
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


BIND = [SD_RA, JR_RA, 0]


def bind_program(*callers):
    return program(BIND, *callers)


def caller_with(sid_words):
    return [SD_RA, *sid_words, 0, JR_RA, 0]


class TestBindSites(unittest.TestCase):
    def test_reads_small_and_large_service_ids_passed_to_the_bind_function(self):
        small = caller_with([addiu(5, 0, 0x30), jal(BASE)])
        large = caller_with([lui(2, 0x8000), ori(5, 2, 0x0400), jal(BASE)])
        image, functions, rows = bind_program(small, large)
        sites = service_bind_sites(image, functions, rows, BASE)
        self.assertEqual(sorted(sites), [0x30, 0x80000400])
        self.assertEqual(sites[0x30], {BASE + 12})

    def test_ignores_calls_to_other_functions(self):
        image, functions, rows = bind_program(caller_with([addiu(5, 0, 0x30), jal(BASE + 4)]))
        self.assertEqual(service_bind_sites(image, functions, rows, BASE), {})

    def test_bind_function_is_the_common_target_after_large_service_ids(self):
        a = caller_with([lui(2, 0x8000), ori(5, 2, 0x0400), jal(BASE)])
        b = caller_with([lui(2, 0x8000), ori(5, 2, 0x0210), jal(BASE)])
        image, functions, rows = bind_program(a, b)
        self.assertEqual(find_bind_function(image, rows), BASE)

    def test_role_comes_from_a_bind_inside_the_callee_closure(self):
        engine = [SQ_RA, jal(BASE + 16), 0, 0]
        wrapper = [SD_RA, addiu(5, 0, 0x30), jal(BASE + 16 + 24), 0, JR_RA, 0]
        bind = [SD_RA, JR_RA, 0, 0, 0, 0]
        image, functions, rows = program(engine, wrapper, bind)
        edges = find_boundary(image, functions, rows)
        sites = service_bind_sites(image, functions, rows, BASE + 16 + 24)
        self.assertEqual(assign_roles(image, functions, rows, edges, sites), {BASE + 16: "audio_sd"})


if __name__ == "__main__":
    unittest.main()
