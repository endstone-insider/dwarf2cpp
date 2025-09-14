import enum
from collections import defaultdict
from dataclasses import dataclass, field
from typing import ClassVar

from ._dwarf import AccessAttribute, VirtualityAttribute


@dataclass
class Object:
    name: str
    parent: "Namespace | None" = field(default=None, compare=False)
    is_implicit: bool = False
    access: AccessAttribute | None = None


@dataclass
class Namespace:
    name: str
    parent: "Namespace | None" = None
    is_inline: bool = False

    @property
    def qualified_name(self) -> str:
        if self.parent is None:
            return self.name or ""

        return f"{self.parent.qualified_name}::{self.name}"


@dataclass
class ImportedModule(Object):
    kind: ClassVar[str] = "imported_module"
    # attributes
    import_: Namespace | None = None


@dataclass
class ImportedDeclaration(Object):
    kind: ClassVar[str] = "imported_declaration"
    # attributes
    import_: Namespace | str | None = None


@dataclass
class Attribute(Object):
    kind: ClassVar[str] = "attribute"
    # attributes
    type: str | None = None
    default_value: int | float | None = None
    alignment: int | None = None
    bit_size: int | None = None
    is_template: bool = False
    is_static: bool = False


class ParameterKind(enum.StrEnum):
    POSITIONAL = "positional"
    VARIADIC = "variadic"


@dataclass
class Parameter:
    name: str
    type: str | None = None
    kind: ParameterKind | None = None


@dataclass
class Function(Object):
    kind: ClassVar[str] = "function"
    # attributes
    parameters: list[Parameter] = field(default_factory=list)
    returns: str | None = None
    noreturn: bool = False
    is_explicit: bool = False
    is_deleted: bool = False
    is_inline: bool = False
    is_static: bool = False
    is_const: bool = False
    virtuality: VirtualityAttribute | None = None


@dataclass
class Struct(Object):
    kind: ClassVar[str] = "struct"
    # attributes
    bases: list[tuple[str, AccessAttribute | None]] = field(default_factory=list)
    members: dict[int, list[Object]] = field(default_factory=lambda: defaultdict(list))
    alignment: int | None = None


@dataclass
class Class(Struct):
    kind: ClassVar[str] = "class"


@dataclass
class Union(Struct):
    kind: ClassVar[str] = "union"


@dataclass
class Enum(Object):
    kind: ClassVar[str] = "enum"
    # attributes
    base: str | None = None
    values: list[tuple[str, int]] = field(default_factory=list)
    is_class: bool = False


@dataclass
class TypeDef(Object):
    kind: ClassVar[str] = "typedef"
    # attributes
    value: str | Struct | Class | Union | Enum | None = None
    alignment: int | None = None
