import unittest

from tests.test_kcet_resolver_keyboard import BASE, EPILOGUE, PROLOGUE, function, synthetic_program
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.mips import addiu, jal, lui, ori
from tools.kcet_resolver.network import INIT_SID, ROLE_SIDS, resolve_network_hooks


def sid_load(sid):
    return [lui(2, sid >> 16), ori(5, 2, sid & 0xFFFF)]


def init_body(sids=None):
    sids = sids if sids is not None else (INIT_SID, *ROLE_SIDS.values())
    return function(*(word for sid in sids for word in (*sid_load(sid), jal(BASE + 0x900), 0)))


def wrapper_body(sid):
    return function(*sid_load(sid), jal(BASE + 0x900), 0)


def synthetic_network(skip=(), extra=()):
    bodies = [init_body()]
    bodies.extend(wrapper_body(sid) for role, sid in ROLE_SIDS.items() if role not in skip)
    bodies.extend(extra)
    return synthetic_program(bodies)


class TestResolveNetworkHooks(unittest.TestCase):
    def test_resolves_each_role_to_its_own_wrapper_not_the_init_function(self):
        image, functions = synthetic_network()
        hooks = resolve_network_hooks(image, functions)
        self.assertEqual(set(hooks), set(ROLE_SIDS))
        self.assertEqual(len(set(hooks.values())), len(ROLE_SIDS))
        init_start = BASE
        self.assertNotIn(init_start, hooks.values())
        for address in hooks.values():
            self.assertEqual(int.from_bytes(image.read(address, 4), "little"), PROLOGUE)

    def test_roles_are_the_documented_seven(self):
        self.assertEqual(set(ROLE_SIDS), {
            "kcet.net.open", "kcet.net.bind", "kcet.net.connect", "kcet.net.send",
            "kcet.net.recv", "kcet.net.recvfrom", "kcet.net.node_id"})

    def test_missing_wrapper_fails(self):
        image, functions = synthetic_network(skip=("kcet.net.send",))
        with self.assertRaises(HookResolutionError):
            resolve_network_hooks(image, functions)

    def test_duplicate_wrapper_for_one_sid_fails(self):
        image, functions = synthetic_network(extra=(wrapper_body(ROLE_SIDS["kcet.net.open"]),))
        with self.assertRaises(HookResolutionError):
            resolve_network_hooks(image, functions)

    def test_missing_init_function_fails(self):
        bodies = [wrapper_body(sid) for sid in ROLE_SIDS.values()]
        image, functions = synthetic_program(bodies)
        with self.assertRaises(HookResolutionError):
            resolve_network_hooks(image, functions)

    def test_wrapper_without_a_call_is_rejected(self):
        bodies = [init_body()]
        bodies.extend(function(*sid_load(sid), addiu(2, 0, 0)) for sid in ROLE_SIDS.values())
        image, functions = synthetic_program(bodies)
        with self.assertRaises(HookResolutionError):
            resolve_network_hooks(image, functions)


if __name__ == "__main__":
    unittest.main()
