"""Errors raised when a KCET hook target cannot be identified unambiguously."""


class HookResolutionError(RuntimeError):
    """Raised when a hook target is missing, ambiguous, or not a verified function start."""
