#!/usr/bin/env python3
"""Sanitized diagnostic classifier for compiler/linker errors.

Classifies factual compiler, linker, runtime-API, or host-portability blockers
into standardized categories without disclosing proprietary code, function names,
hex addresses, or disassembly.
"""
from __future__ import annotations

import argparse
import enum
import json
import re
import sys
from pathlib import Path
from typing import Any, Dict, List

class BlockerCategory(str, enum.Enum):
    GENERATED_SOURCE_SYNTAX_OR_CODEGEN = "generated_source_syntax_or_codegen"
    MISSING_PS2RECOMP_RUNTIME_API = "missing_ps2recomp_runtime_api"
    MISSING_HOST_DEPENDENCY = "missing_host_dependency"
    HOST_COMPILER_OR_STANDARD_LIBRARY_INCOMPATIBILITY = "host_compiler_or_standard_library_incompatibility"
    HOST_VECTOR_OR_SIMD_PORTABILITY = "host_vector_or_simd_portability"
    UNRESOLVED_GENERATED_SYMBOL = "unresolved_generated_symbol"
    LINKER_OR_BUILD_GRAPH = "linker_or_build_graph"
    RESOURCE_OR_TOOLCHAIN_LIMIT = "resource_or_toolchain_limit"
    UNKNOWN_REQUIRES_REVIEW = "unknown_requires_review"

class DiagnosticClassifier:
    def __init__(self):
        pass

    def classify_line(self, line: str) -> BlockerCategory:
        line_lower = line.lower()

        # 1. Resource or toolchain limits (OOM, killed, disk full)
        if any(term in line_lower for term in ["killed signal terminated", "out of memory", "no space left on device", "virtual memory exhausted"]):
            return BlockerCategory.RESOURCE_OR_TOOLCHAIN_LIMIT

        # 2. Host SIMD / Vector issues
        if any(term in line_lower for term in ["needs isa option", "sse", "avx", "neon", "simd", "vector", "cannot be used with"]):
            if "option -m" in line_lower or "isa option" in line_lower:
                return BlockerCategory.HOST_VECTOR_OR_SIMD_PORTABILITY

        # 3. Missing host dependency (headers like raylib.h, libavcodec, SDL, etc.)
        if any(term in line_lower for term in ["fatal error:", "no such file or directory", "arquivo ou diretório inexistente"]):
            if any(h in line_lower for h in [".h", ".hpp", "package", "not found"]):
                return BlockerCategory.MISSING_HOST_DEPENDENCY

        # 4. Compiler / C++ standard library incompatibility
        if any(term in line_lower for term in ["requires compiler and library support", "iso c++", "c++20", "-std=c++", "unsupported compiler"]):
            return BlockerCategory.HOST_COMPILER_OR_STANDARD_LIBRARY_INCOMPATIBILITY

        # 5. Missing PS2Recomp runtime API (members on PS2Runtime, missing runtime macros, undefined reference to PS2Runtime::*)
        if ("'class ps2runtime'" in line_lower or "ps2runtime" in line_lower) and any(term in line_lower for term in ["has no member", "not declared in this scope", "undefined member", "undefined reference to `ps2runtime::", "undefined reference to \"ps2runtime::", "referência não definida para \"ps2runtime::", "referência não definida para `ps2runtime::"]):
            return BlockerCategory.MISSING_PS2RECOMP_RUNTIME_API

        # 6. Unresolved generated symbol (undefined reference to FUN_, entry_, sub_)
        if any(term in line_lower for term in ["undefined reference", "referência não definida"]):
            if any(prefix in line for prefix in ["FUN_", "entry_", "sub_", "func_", "j_func_"]):
                return BlockerCategory.UNRESOLVED_GENERATED_SYMBOL
            if "ps2runtime::" in line_lower:
                return BlockerCategory.MISSING_PS2RECOMP_RUNTIME_API
            if "g_ps2recompiledfunctiontable" in line_lower:
                return BlockerCategory.MISSING_PS2RECOMP_RUNTIME_API
            if "main" in line_lower or "collect2" in line_lower or "_start" in line_lower:
                return BlockerCategory.LINKER_OR_BUILD_GRAPH
            return BlockerCategory.UNRESOLVED_GENERATED_SYMBOL

        # 7. General linker or build graph
        if any(term in line_lower for term in ["collect2: error: ld returned", "undefined reference to 'main'", "cannot find -l", "multiple definition of"]):
            return BlockerCategory.LINKER_OR_BUILD_GRAPH

        # 8. Generated source syntax or codegen errors
        if any(term in line_lower for term in ["expected ';'", "error:", "syntax error", "stray '\\", "invalid conversion"]):
            if any(term in line_lower for term in ["generated/", ".cpp", "sub_", "fun_"]):
                return BlockerCategory.GENERATED_SOURCE_SYNTAX_OR_CODEGEN

        return BlockerCategory.UNKNOWN_REQUIRES_REVIEW

    def sanitize_message(self, line: str) -> str:
        """Strip file paths, hexadecimal addresses, and function names."""
        sanitized = line
        # Replace hex addresses: 0x12345678
        sanitized = re.sub(r'0x[0-9a-fA-F]+', '<HEX_ADDR>', sanitized)
        # Replace function names: FUN_xxx, sub_xxx, entry_xxx
        sanitized = re.sub(r'(FUN|sub|entry)_[0-9a-zA-Z_]+', '<GUEST_FUNC>', sanitized)
        # Replace disassembly/assembly instructions: j func_xxx, addiu, etc.
        sanitized = re.sub(r'\b(j|jal|beq|bne|jr|jalr)\s+[a-zA-Z0-9_]+', '<ASM_JUMP>', sanitized)
        sanitized = re.sub(r'func_[0-9a-zA-Z_]+', '<GUEST_LABEL>', sanitized)
        # Replace file paths
        sanitized = re.sub(r'(/[a-zA-Z0-9_\-\.]+)+/([a-zA-Z0-9_\-\.]+)', r'<PATH>/\2', sanitized)
        return sanitized.strip()

    def classify_text(self, text: str) -> Dict[str, Any]:
        lines = [line.strip() for line in text.splitlines() if line.strip()]
        counts: Dict[str, int] = {}
        samples: Dict[str, List[str]] = {}

        for line in lines:
            if not any(term in line.lower() for term in ["error", "fatal", "undefined reference", "referência não definida", "killed"]):
                continue

            category = self.classify_line(line)
            cat_name = category.value
            counts[cat_name] = counts.get(cat_name, 0) + 1

            if cat_name not in samples:
                samples[cat_name] = []
            if len(samples[cat_name]) < 3:
                sanitized_sample = self.sanitize_message(line)
                if sanitized_sample not in samples[cat_name]:
                    samples[cat_name].append(sanitized_sample)

        return {
            "category_counts": counts,
            "sanitized_samples": samples,
            "total_diagnostics_classified": sum(counts.values()),
        }

def main():
    parser = argparse.ArgumentParser(description="Classify sanitized compiler/linker diagnostics.")
    parser.add_argument("--log", type=Path, required=True, help="Path to raw compiler log")
    parser.add_argument("--out", type=Path, default=None, help="Output path for sanitized JSON report")
    args = parser.parse_args()

    if not args.log.exists():
        sys.stderr.write(f"Error: Log file not found: {args.log}\n")
        sys.exit(1)

    raw_text = args.log.read_text(encoding="utf-8", errors="replace")
    classifier = DiagnosticClassifier()
    report = classifier.classify_text(raw_text)

    output_json = json.dumps(report, indent=2) + "\n"
    if args.out:
        args.out.write_text(output_json, encoding="utf-8")
        print(f"Sanitized diagnostic report written to {args.out}")
    else:
        print(output_json)

if __name__ == "__main__":
    main()
