import lazy_loader as lazy

__getattr__, __dir__, __all__ = lazy.attach(
    __name__,
    submodules=set(),
    submod_attrs={
        "_dwarf": [
            "AccessAttribute",
            "DWARFAttribute",
            "DWARFContext",
            "DWARFDie",
            "DWARFUnit",
            "DWARFTypePrinter",
            "VirtualityAttribute",
        ],
    },
)
