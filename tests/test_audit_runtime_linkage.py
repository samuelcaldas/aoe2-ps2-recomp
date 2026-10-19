#!/usr/bin/env python3
"""
Unit tests for tools/audit_runtime_linkage.py.

Verifies classification of unresolved generic runtime symbols against:
- runtime_target_not_linked
- library_order_or_as_needed
- runtime_abi_signature_mismatch
- runtime_definition_absent
- generated_registration_source_omitted
- inconclusive_requires_review
"""

import unittest
from tools.audit_runtime_linkage import (
    RuntimeLinkAudit,
    AuditClassification,
    SymbolEvidence,
)

class TestRuntimeLinkAudit(unittest.TestCase):
    def setUp(self):
        self.auditor = RuntimeLinkAudit()

    def test_runtime_target_not_linked(self):
        """Symbol is defined in a known runtime artifact, but the artifact is absent from link command."""
        sym_evidence = SymbolEvidence(
            symbol_name="PS2Runtime::Store128(unsigned char*, R5900Context*, unsigned int, long long __vector(2))",
            demangled_name="PS2Runtime::Store128",
            defined_in_runtime_source=True,
            runtime_target_in_build_graph=True,
            runtime_target_in_link_command=False,
            is_generated_table_symbol=False,
        )
        classification = self.auditor.classify_symbol(sym_evidence)
        self.assertEqual(classification, AuditClassification.RUNTIME_TARGET_NOT_LINKED)

    def test_library_order_or_as_needed(self):
        """Symbol is in a runtime archive present in the link command, but preceded by --as-needed or misplaced."""
        sym_evidence = SymbolEvidence(
            symbol_name="PS2Runtime::dispatchGuestBranch",
            demangled_name="PS2Runtime::dispatchGuestBranch",
            defined_in_runtime_source=True,
            runtime_target_in_build_graph=True,
            runtime_target_in_link_command=True,
            library_order_issue=True,
            is_generated_table_symbol=False,
        )
        classification = self.auditor.classify_symbol(sym_evidence)
        self.assertEqual(classification, AuditClassification.LIBRARY_ORDER_OR_AS_NEEDED)

    def test_runtime_abi_signature_mismatch(self):
        """Symbol exists in runtime with different argument/return ABI."""
        sym_evidence = SymbolEvidence(
            symbol_name="PS2Runtime::SomeCall(int)",
            demangled_name="PS2Runtime::SomeCall",
            defined_in_runtime_source=True,
            abi_signature_mismatch=True,
            runtime_target_in_build_graph=True,
            runtime_target_in_link_command=True,
            is_generated_table_symbol=False,
        )
        classification = self.auditor.classify_symbol(sym_evidence)
        self.assertEqual(classification, AuditClassification.RUNTIME_ABI_SIGNATURE_MISMATCH)

    def test_runtime_definition_absent(self):
        """Symbol is completely absent from all pinned PS2Recomp runtime sources and archives."""
        sym_evidence = SymbolEvidence(
            symbol_name="PS2Runtime::NonExistentGenericApi()",
            demangled_name="PS2Runtime::NonExistentGenericApi",
            defined_in_runtime_source=False,
            runtime_target_in_build_graph=False,
            runtime_target_in_link_command=False,
            is_generated_table_symbol=False,
        )
        classification = self.auditor.classify_symbol(sym_evidence)
        self.assertEqual(classification, AuditClassification.RUNTIME_DEFINITION_ABSENT)

    def test_generated_registration_source_omitted(self):
        """Symbol belongs to generated table/registration glue and was omitted from compilation/manifest."""
        sym_evidence = SymbolEvidence(
            symbol_name="g_ps2RecompiledFunctionTableSlotCount",
            demangled_name="g_ps2RecompiledFunctionTableSlotCount",
            defined_in_runtime_source=False,
            is_generated_table_symbol=True,
            generated_source_present_on_disk=True,
            generated_source_included_in_manifest=True,
            generated_source_compiled_in_target=False,
        )
        classification = self.auditor.classify_symbol(sym_evidence)
        self.assertEqual(classification, AuditClassification.GENERATED_REGISTRATION_SOURCE_OMITTED)

    def test_inconclusive_requires_review(self):
        """Unknown or contradictory evidence."""
        sym_evidence = SymbolEvidence(
            symbol_name="SomeUnknownSymbol",
            demangled_name="SomeUnknownSymbol",
            defined_in_runtime_source=None,
        )
        classification = self.auditor.classify_symbol(sym_evidence)
        self.assertEqual(classification, AuditClassification.INCONCLUSIVE_REQUIRES_REVIEW)

    def test_sanitization_rules_enforced(self):
        """Ensure report generation never leaks proprietary hex addresses or game function names."""
        report = self.auditor.generate_report(
            ps2recomp_commit="eb8688b2fd191129f103a39543002c005b33409c",
            toolchain_info={"compiler": "GCC 15.2.0", "linker": "GNU ld 2.46"},
            runtime_artifacts={"libps2_runtime.a": "absent"},
            symbol_evaluations=[
                SymbolEvidence(
                    symbol_name="PS2Runtime::Store128(unsigned char*, R5900Context*, unsigned int, long long __vector(2))",
                    demangled_name="PS2Runtime::Store128",
                    defined_in_runtime_source=True,
                    runtime_target_in_build_graph=True,
                    runtime_target_in_link_command=False,
                )
            ],
            raw_log_path="build/desktop-probe/raw_diagnostics.log",
        )
        # Check that report has required keys
        self.assertIn("ps2recomp_commit", report)
        self.assertIn("host_toolchain", report)
        self.assertIn("runtime_artifacts", report)
        self.assertIn("classifications", report)
        self.assertIn("aggregate_counts", report)
        self.assertIn("ignored_raw_log_location", report)

        # Ensure no guest addresses (0x...) or guest function names (FUN_...) are in keys or values
        report_str = str(report)
        self.assertNotIn("0x1000", report_str)
        self.assertNotIn("FUN_", report_str)
        self.assertNotIn("sub_", report_str)

if __name__ == "__main__":
    unittest.main()
