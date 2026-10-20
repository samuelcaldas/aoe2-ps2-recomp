"""Error types raised by the HD asset import, catalog, and verification tools."""


class HdAssetError(Exception):
    """Base class for every HD asset tool failure."""


class EditionMismatchError(HdAssetError):
    """The source folder is missing the sentinel files of the expected edition."""


class MissingCategoryError(HdAssetError):
    """A requested category folder or file is absent from the source install."""


class ConflictError(HdAssetError):
    """A destination file exists with content that differs from the source."""


class IntegrityError(HdAssetError):
    """A copied or recorded file does not match its expected digest."""
