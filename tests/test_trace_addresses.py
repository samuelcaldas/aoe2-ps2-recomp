#!/usr/bin/env python3
"""Extracting guest function addresses from a decomp module for the call-trace list."""
import unittest

from tools.trace_addresses import TraceListError, parse_function_addresses, render_trace_list

SOURCE = """
int func_00134210(int a);            /* forward declaration only */
/* func_00134140: comment */
int func_00134140(void) {
    return 0;
}
static const char *func_00134AF0(int status)
{
    return 0;
}
void func_0012CCD8(void) {}
int func_00134140(void) { return 1; }  /* duplicate body */
"""


class TestTraceAddresses(unittest.TestCase):
    def test_only_functions_with_bodies_are_listed(self):
        addresses = parse_function_addresses(SOURCE)
        self.assertEqual(addresses, [0x12CCD8, 0x134140, 0x134AF0])

    def test_declarations_are_ignored(self):
        self.assertNotIn(0x134210, parse_function_addresses(SOURCE))

    def test_empty_module_fails_fast(self):
        with self.assertRaises(TraceListError):
            parse_function_addresses("int x;")

    def test_rendered_list_is_one_hex_address_per_line(self):
        self.assertEqual(render_trace_list([0x134140, 0x12CCD8]), "0x12ccd8\n0x134140\n")


if __name__ == "__main__":
    unittest.main()
