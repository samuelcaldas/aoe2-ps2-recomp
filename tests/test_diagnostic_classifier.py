import json
import tempfile
import unittest
from pathlib import Path

# We import the classifier module that we will write
# First, let's write the test cases that must fail (RED) before tools/classify_diagnostics.py exists.
from tools.classify_diagnostics import DiagnosticClassifier, BlockerCategory

class TestDiagnosticClassifier(unittest.TestCase):
    def setUp(self):
        self.classifier = DiagnosticClassifier()

    def test_generated_source_syntax_or_codegen(self):
        log = "generated/SLES-50282/FUN_001000b8_0x1000b8.cpp:45:10: error: expected ';' before '}' token"
        result = self.classifier.classify_line(log)
        self.assertEqual(result, BlockerCategory.GENERATED_SOURCE_SYNTAX_OR_CODEGEN)

    def test_missing_ps2recomp_runtime_api(self):
        log = "generated/SLES-50282/FUN_001000b8.cpp:12:15: error: 'class PS2Runtime' has no member named 'lookupFunction'"
        result = self.classifier.classify_line(log)
        self.assertEqual(result, BlockerCategory.MISSING_PS2RECOMP_RUNTIME_API)

    def test_missing_host_dependency(self):
        log = "third_party/PS2Recomp/ps2xRuntime/include/ps2_host_backend.h:3:10: fatal error: raylib.h: No such file or directory"
        result = self.classifier.classify_line(log)
        self.assertEqual(result, BlockerCategory.MISSING_HOST_DEPENDENCY)

    def test_host_compiler_or_standard_library_incompatibility(self):
        log = "error: #error This file requires compiler and library support for the ISO C++ 2020 standard."
        result = self.classifier.classify_line(log)
        self.assertEqual(result, BlockerCategory.HOST_COMPILER_OR_STANDARD_LIBRARY_INCOMPATIBILITY)

    def test_host_vector_or_simd_portability(self):
        log = "error: '__builtin_ia32_paddw' needs isa option -msse2"
        result = self.classifier.classify_line(log)
        self.assertEqual(result, BlockerCategory.HOST_VECTOR_OR_SIMD_PORTABILITY)

    def test_unresolved_generated_symbol(self):
        log = "/tmp/test.o: in function 'test': (.text+0x10): undefined reference to 'FUN_00104940_0x104940(unsigned char*, R5900Context*, PS2Runtime*)'"
        result = self.classifier.classify_line(log)
        self.assertEqual(result, BlockerCategory.UNRESOLVED_GENERATED_SYMBOL)

    def test_linker_or_build_graph(self):
        log = "/usr/bin/ld: /usr/lib/Scrt1.o: in function '_start': (.text+0x1b): undefined reference to 'main'\ncollect2: error: ld returned 1 exit status"
        result = self.classifier.classify_line(log)
        self.assertEqual(result, BlockerCategory.LINKER_OR_BUILD_GRAPH)

    def test_resource_or_toolchain_limit(self):
        log = "c++: fatal error: Killed signal terminated program cc1plus"
        result = self.classifier.classify_line(log)
        self.assertEqual(result, BlockerCategory.RESOURCE_OR_TOOLCHAIN_LIMIT)

    def test_unknown_requires_review(self):
        log = "some completely unexpected internal compiler anomaly 9999"
        result = self.classifier.classify_line(log)
        self.assertEqual(result, BlockerCategory.UNKNOWN_REQUIRES_REVIEW)

    def test_sanitization_removes_proprietary_details(self):
        raw_log = (
            "generated/SLES-50282/FUN_001000b8_0x1000b8.cpp:45:10: error: "
            "undefined reference to 'FUN_00104940_0x104940'\n"
            "0x1000b8: 0x8041250 j func_104940\n"
        )
        report = self.classifier.classify_text(raw_log)
        report_json = json.dumps(report)
        # Ensure raw function names, hex addresses, or disassembly are not in the report
        self.assertNotIn("FUN_001000b8", report_json)
        self.assertNotIn("FUN_00104940", report_json)
        self.assertNotIn("0x1000b8", report_json)
        self.assertNotIn("func_104940", report_json)
