import enum
from collections import defaultdict
from dataclasses import dataclass, field
from typing import ClassVar

from ._dwarf import AccessAttribute, VirtualityAttribute


@dataclass
class Object:
    kind: ClassVar[str]

    name: str
    parent: "Namespace | None" = field(default=None, compare=False)
    is_implicit: bool = False
    is_declaration: bool = False
    access: AccessAttribute | None = None
    template: "Template | None" = field(default=None, compare=False)

    def merge(self, other: "Object") -> bool:
        return False


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
    type: str | tuple[str, str] | None = None
    default_value: int | float | None = None
    alignment: int | None = None
    bit_size: int | None = None
    is_static: bool = False

    def merge(self, other: "Object") -> bool:
        if not isinstance(other, Attribute):
            return False

        if self.name != other.name or self.type != other.type:
            return False

        if self.default_value is None:
            self.default_value = other.default_value

        self.alignment = self.alignment or other.alignment
        self.bit_size = self.bit_size or other.bit_size
        self.is_static = self.is_static or other.is_static
        return True


class ParameterKind(enum.StrEnum):
    POSITIONAL = "positional"
    VARIADIC = "variadic"


@dataclass
class Parameter:
    name: str
    type: str | tuple[str, str] | None = None
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

    def merge(self, other: "Function") -> bool:
        if not isinstance(other, Function):
            return False

        if self.name != other.name or self.returns != other.returns or len(self.parameters) != len(other.parameters):
            return False

        for p1, p2 in zip(self.parameters, other.parameters):
            if p1.type != p2.type or p1.kind != p2.kind:
                return False

        for p1, p2 in zip(self.parameters, other.parameters):
            p1.name = p1.name or p2.name

        self.noreturn = self.noreturn or other.noreturn
        self.is_explicit = self.is_explicit or other.is_explicit
        self.is_deleted = self.is_deleted or other.is_deleted
        self.is_inline = self.is_inline or other.is_inline
        self.is_static = self.is_static or other.is_static
        self.is_const = self.is_const or other.is_const
        self.virtuality = self.virtuality or other.virtuality
        return True


@dataclass
class Struct(Object):
    kind: ClassVar[str] = "struct"
    # attributes
    bases: list[tuple[str, AccessAttribute | None]] = field(default_factory=list)
    members: dict[int, list[Object]] = field(default_factory=lambda: defaultdict(list))
    alignment: int | None = None

    def merge(self, other: "Struct") -> bool:
        if not isinstance(other, Struct):
            return False

        if self.kind != other.kind or self.name != other.name or self.bases != other.bases:
            return False

        for lineno, objects in other.members.items():
            members = self.members[lineno]
            members.extend(objects)

            result = []
            for item in members:
                if not result:
                    # First item, just add it
                    result.append(item)
                elif item not in result:
                    last = result[-1]
                    if not last.merge(item):
                        # merge() returned False, so append new item
                        result.append(item)

            self.members[lineno] = result

        self.alignment = self.alignment or other.alignment
        return True


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


class TemplateParameterKind(enum.StrEnum):
    CONSTANT = "constant"
    TYPE = "type"
    TEMPLATE = "template"
    PACK = "pack"


@dataclass
class TemplateParameter:
    name: str
    kind: TemplateParameterKind
    type: str | None = None
    arg: str | None = field(default=None, compare=False)
    default: str | None = None


@dataclass
class Template(Object):
    kind: ClassVar[str] = "template"
    declaration: Struct | Attribute | None = None
    parameters: list[TemplateParameter] = field(default_factory=list)

    def merge(self, other: "Template") -> bool:
        if not isinstance(other, Template):
            return False

        if (
            self.name != other.name
            or self.declaration != other.declaration
            or len(self.parameters) != len(other.parameters)
        ):
            return False

        for p1, p2 in zip(self.parameters, other.parameters):
            if (
                p1.name != p2.name
                or p1.kind != p2.kind
                or p1.type != p2.type
                or p1.arg is not None
                or p2.arg is not None
            ):
                return False

        for p1, p2 in zip(self.parameters, other.parameters):
            p1.default = p1.default or p2.default

        return True
