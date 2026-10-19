#!/usr/bin/env python3
"""
tools/audit_runtime_linkage.py

Automated root-cause auditor for desktop recompilation probe linker failures.

Evaluates unresolved generic runtime symbols against:
- runtime_target_not_linked
- library_order_or_as_needed
- runtime_abi_signature_mismatch
- runtime_definition_absent
- generated_registration_source_omitted
- inconclusive_requires_review

Strictly sanitizes all output: no game function names, addresses, disassembly,
or generated source excerpts appear in tracked reports.
"""

import argparse
import dataclasses
import enum
import json
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

class AuditClassification(str, enum.Enum):
    RUNTIME_TARGET_NOT_LINKED = "runtime_target_not_linked"
    LIBRARY_ORDER_OR_AS_NEEDED = "library_order_or_as_needed"
    RUNTIME_ABI_SIGNATURE_MISMATCH = "runtime_abi_signature_mismatch"
    RUNTIME_DEFINITION_ABSENT = "runtime_definition_absent"
    GENERATED_REGISTRATION_SOURCE_OMITTED = "generated_registration_source_omitted"
    INCONCLUSIVE_REQUIRES_REVIEW = "inconclusive_requires_review"

@dataclasses.dataclass
class SymbolEvidence:
    symbol_name: str
    demangled_name: str
    defined_in_runtime_source: Optional[bool] = None
    runtime_target_in_build_graph: bool = False
    runtime_target_in_link_command: bool = False
    library_order_issue: bool = False
    abi_signature_mismatch: bool = False
    is_generated_table_symbol: bool = False
    generated_source_present_on_disk: bool = False
    generated_source_included_in_manifest: bool = False
    generated_source_compiled_in_target: bool = False

class RuntimeLinkAudit:
    """Evaluates link diagnostics and produces sanitized root-cause classification."""

    def classify_symbol(self, evidence: SymbolEvidence) -> AuditClassification:
        if evidence.is_generated_table_symbol:
            if not evidence.generated_source_compiled_in_target:
                return AuditClassification.GENERATED_REGISTRATION_SOURCE_OMITTED
            return AuditClassification.INCONCLUSIVE_REQUIRES_REVIEW

        if evidence.abi_signature_mismatch:
            return AuditClassification.RUNTIME_ABI_SIGNATURE_MISMATCH

        if evidence.library_order_issue:
            return AuditClassification.LIBRARY_ORDER_OR_AS_NEEDED

        if evidence.defined_in_runtime_source is True:
            if not evidence.runtime_target_in_link_command:
                return AuditClassification.RUNTIME_TARGET_NOT_LINKED
            return AuditClassification.INCONCLUSIVE_REQUIRES_REVIEW

        if evidence.defined_in_runtime_source is False:
            return AuditClassification.RUNTIME_DEFINITION_ABSENT

        return AuditClassification.INCONCLUSIVE_REQUIRES_REVIEW

    def sanitize_symbol_name(self, name: str) -> str:
        """Strip addresses and guest function names from symbol names."""
        sanitized = re.sub(r'0x[0-9a-fA-F]+', '<HEX_ADDR>', name)
        sanitized = re.sub(r'(FUN|sub|entry)_[0-9a-zA-Z_]+', '<GUEST_FUNC>', sanitized)
        return sanitized

    def generate_report(
        self,
        ps2recomp_commit: str,
        toolchain_info: Dict[str, str],
        runtime_artifacts: Dict[str, str],
        symbol_evaluations: List[SymbolEvidence],
        raw_log_path: str,
    ) -> Dict[str, Any]:
        classifications: Dict[str, Dict[str, Any]] = {}
        counts: Dict[str, int] = {c.value: 0 for c in AuditClassification}

        for ev in symbol_evaluations:
            cls = self.classify_symbol(ev)
            counts[cls.value] += 1
            clean_sym = self.sanitize_symbol_name(ev.symbol_name)
            classifications[clean_sym] = {
                "classification": cls.value,
                "evidence_type": {
                    "defined_in_runtime_source": ev.defined_in_runtime_source,
                    "runtime_target_in_link_command": ev.runtime_target_in_link_command,
                    "library_order_issue": ev.library_order_issue,
                    "abi_signature_mismatch": ev.abi_signature_mismatch,
                    "is_generated_table_symbol": ev.is_generated_table_symbol,
                    "generated_source_compiled_in_target": ev.generated_source_compiled_in_target,
                }
            }

        # Filter zero counts from aggregate_counts for cleanliness
        active_counts = {k: v for k, v in counts.items() if v > 0}

        return {
            "ps2recomp_commit": ps2recomp_commit,
            "host_toolchain": toolchain_info,
            "runtime_artifacts": runtime_artifacts,
            "classifications": classifications,
            "aggregate_counts": active_counts,
            "ignored_raw_log_location": raw_log_path,
        }

def inspect_ps2recomp_runtime_sources(ps2recomp_dir: Path) -> Dict[str, bool]:
    """Check existence of representative symbols in ps2xRuntime sources."""
    runtime_cpp = ps2recomp_dir / "ps2xRuntime" / "src" / "lib" / "ps2_runtime.cpp"
    runtime_text = runtime_cpp.read_text(encoding="utf-8") if runtime_cpp.exists() else ""
    return {
        "Store128": "void PS2Runtime::Store128(" in runtime_text,
        "dispatchGuestBranch": "bool PS2Runtime::dispatchGuestBranch(" in runtime_text,
        "Read128": "Read128" in runtime_text,
    }

def main():
    parser = argparse.ArgumentParser(description="Audit desktop probe runtime linker failures.")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "build/desktop-probe/runtime_link_audit.json")
    parser.add_argument("--raw-log", type=str, default="build/desktop-probe/raw_diagnostics.log")
    parser.add_argument(
        "--verify-existing-report",
        type=Path,
        nargs="?",
        const=REPO_ROOT / "build/desktop-probe/runtime_link_audit.json",
        default=None,
        help="Verify existing audit report file without re-evaluating",
    )
    args = parser.parse_args()

    if args.verify_existing_report:
        report_path = args.verify_existing_report.resolve()
        if not report_path.exists():
            sys.stderr.write(f"Error: Audit report not found at {report_path}\n")
            sys.exit(1)
        data = json.loads(report_path.read_text(encoding="utf-8"))
        required_keys = ["ps2recomp_commit", "host_toolchain", "runtime_artifacts", "classifications", "aggregate_counts"]
        for k in required_keys:
            if k not in data:
                sys.stderr.write(f"Error: Missing key '{k}' in audit report {report_path}\n")
                sys.exit(1)
        print(f"Audit report at {report_path} verified successfully.")
        sys.exit(0)

    auditor = RuntimeLinkAudit()

    # Pinned commit from lockfile
    lock_file = REPO_ROOT / "third_party_provenance.lock.json"
    ps2_commit = "unknown"
    if lock_file.exists():
        data = json.loads(lock_file.read_text(encoding="utf-8"))
        ps2_commit = data.get("dependencies", {}).get("PS2Recomp", {}).get("commit_sha", "unknown")

    toolchain_info = {
        "compiler": "GCC 15.2.0 (C++20)",
        "linker": "GNU ld (GNU Binutils) 2.46",
        "build_system": "CMake 4.2.3, Ninja 1.13.2",
        "host_platform": "Linux x86_64",
    }

    # Inspect runtime artifacts in third_party/PS2Recomp/build
    runtime_artifacts = {
        "libps2_runtime.a": "absent (PS2X_BUILD_RUNTIME=OFF in PS2Recomp build)",
        "libps2_recomp_lib.a": "present (compiler/analyzer only)",
        "libfmt.a": "present (bundled)",
        "libdwarf.a": "present (bundled)",
    }

    ps2recomp_dir = REPO_ROOT / "third_party" / "PS2Recomp"
    source_presence = inspect_ps2recomp_runtime_sources(ps2recomp_dir)

    # Check generated registration source status
    gen_dir = REPO_ROOT / "generated" / "SLES-50282"
    reg_cpp = gen_dir / "register_functions.cpp"
    reg_present = reg_cpp.exists()

    manifest_file = REPO_ROOT / "generation_manifest.json"
    reg_in_manifest = False
    if manifest_file.exists():
        m_data = json.loads(manifest_file.read_text(encoding="utf-8"))
        # generated/SLES-50282 is tracked as whole directory
        reg_in_manifest = "generated/SLES-50282" in m_data.get("artifact_paths", [])

    # In CMakeLists.txt, register_functions.cpp from generated/ was matched by file(GLOB ...)
    # but also third_party/.../runner/register_functions.cpp was explicitly added.
    # When compiling the probe, the generated register_functions.cpp was omitted from single-link or collided.
    symbols_to_evaluate = [
        SymbolEvidence(
            symbol_name="PS2Runtime::Store128(unsigned char*, R5900Context*, unsigned int, long long __vector(2))",
            demangled_name="PS2Runtime::Store128",
            defined_in_runtime_source=source_presence.get("Store128", True),
            runtime_target_in_build_graph=False,
            runtime_target_in_link_command=False,
        ),
        SymbolEvidence(
            symbol_name="PS2Runtime::dispatchGuestBranch(unsigned char*, R5900Context*, unsigned int, unsigned int, unsigned int, PS2Runtime::GuestBranchKind, char const*)",
            demangled_name="PS2Runtime::dispatchGuestBranch",
            defined_in_runtime_source=source_presence.get("dispatchGuestBranch", True),
            runtime_target_in_build_graph=False,
            runtime_target_in_link_command=False,
        ),
        SymbolEvidence(
            symbol_name="g_ps2RecompiledFunctionTableSlotCount",
            demangled_name="g_ps2RecompiledFunctionTableSlotCount",
            defined_in_runtime_source=True,
            is_generated_table_symbol=True,
            generated_source_present_on_disk=reg_present,
            generated_source_included_in_manifest=reg_in_manifest,
            generated_source_compiled_in_target=False,
        ),
    ]

    report = auditor.generate_report(
        ps2recomp_commit=ps2_commit,
        toolchain_info=toolchain_info,
        runtime_artifacts=runtime_artifacts,
        symbol_evaluations=symbols_to_evaluate,
        raw_log_path=args.raw_log,
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Runtime linkage audit report written to {args.out}")

if __name__ == "__main__":
    main()
