import functools
import logging
import posixpath
import struct
from collections import defaultdict
from typing import Callable, Generator

from ._dwarf import (
    AccessAttribute,
    DWARFContext,
    DWARFDie,
    DWARFTypePrinter,
    InlineAttribute,
    VirtualityAttribute,
)
from .models import (
    Attribute,
    Class,
    Enum,
    Function,
    ImportedDeclaration,
    ImportedModule,
    Namespace,
    Object,
    Parameter,
    ParameterKind,
    Struct,
    TypeDef,
    Union,
)

logger = logging.getLogger("dwarf2cpp")


def scoped_tags(tag: str) -> bool:
    return tag in {
        "DW_TAG_structure_type",
        "DW_TAG_class_type",
        "DW_TAG_union_type",
        "DW_TAG_namespace",
        "DW_TAG_enumeration_type",
        "DW_TAG_typedef",
    }


@functools.cache
def get_qualified_type(die: DWARFDie):
    printer = DWARFTypePrinter()
    if scoped_tags(die.tag):
        printer.append_scopes(die.parent)

    printer.append_unqualified_name(die)
    return str(printer)


class Visitor:
    """
    Visitor iterators on compile units to extract data from them.
    """

    def __init__(self, context: DWARFContext, base_dir: str):
        self.context = context
        self._files: dict[str, dict[int, list[Object]]] = defaultdict(lambda: defaultdict(list))
        self._base_dir = base_dir
        self._cache = {}
        self._param_names: dict[str, list[str]] = {}
        self._functions: dict[str, list[Function]] = defaultdict(list)

    @property
    def files(self) -> Generator[tuple[str, dict[int, list[Object]]]]:
        """Build and return the files attached to this visitor.

        This method triggers a complete visit of the compile units.

        Returns:
            List of files
        """
        for i, cu in enumerate(self.context.compile_units):
            cu_die = cu.unit_die
            name = cu_die.short_name
            num_compile_units = self.context.num_compile_units
            compilation_dir = cu.compilation_dir.replace("\\", "/")
            if not compilation_dir.startswith(self._base_dir):
                logger.info(f"[{i + 1}/{num_compile_units}] Skipping compile unit {name} ({compilation_dir})")
                continue

            logger.info(f"[{i + 1}/{num_compile_units}] Visiting compile unit {name} ({compilation_dir})")
            self.visit(cu_die)

        for key, param_names in self._param_names.items():
            functions = self._functions[key]
            for function in functions:
                for i, param in enumerate(function.parameters):
                    if param.name is None:
                        param.name = param_names[i]

        # merge file with others that have the same relative path
        for path, file in self._files.items():
            rel_path = str(posixpath.relpath(path, self._base_dir))
            if rel_path.startswith("../"):
                continue

            for line, objects in file.items():
                # ensure the uniqueness of elements
                if len(objects) > 1:
                    seen = []
                    for obj in objects:
                        if obj not in seen:
                            seen.append(obj)
                    objects = seen

                file[line] = objects

            yield rel_path, file

    def visit(self, die: DWARFDie) -> None:
        if die.offset in self._cache:
            return

        kind = die.tag.split("DW_TAG_", maxsplit=1)[1]
        func: Callable[[DWARFDie], None] = getattr(self, f"visit_{kind}", self.generic_visit)
        func(die)

    def visit_compile_unit(self, die: DWARFDie):
        """Visit a compile unit"""
        for child in die.children:
            if child.tag == "DW_TAG_namespace":
                self.visit(child)
            elif child.tag in {
                "DW_TAG_typedef",
                "DW_TAG_class_type",
                "DW_TAG_enumeration_type",
                "DW_TAG_union_type",
                "DW_TAG_structure_type",
                "DW_TAG_variable",
                "DW_TAG_subprogram",
                "DW_TAG_imported_module",
                "DW_TAG_imported_declaration",
            }:
                if not child.find("DW_AT_decl_file"):
                    continue

                decl_file = posixpath.abspath(child.decl_file.replace("\\", "/"))
                if not decl_file.startswith(self._base_dir):
                    continue

                decl_line = child.decl_line
                if not decl_line:
                    continue

                self.visit(child)
                obj = self._cache.get(child.offset, None)
                if obj is not None:
                    self._add(decl_file, decl_line, obj)

            elif child.tag in {
                "DW_TAG_base_type",
                "DW_TAG_array_type",
                "DW_TAG_const_type",
                "DW_TAG_pointer_type",
                "DW_TAG_reference_type",
                "DW_TAG_rvalue_reference_type",
                "DW_TAG_atomic_type",
                "DW_TAG_volatile_type",
                "DW_TAG_restrict_type",
                "DW_TAG_unspecified_type",
                "DW_TAG_subroutine_type",
                "DW_TAG_ptr_to_member_type",
                "DW_TAG_label",
            }:
                pass
            else:
                raise ValueError(f"Unhandled child tag {child.tag}")

    def visit_namespace(self, die: DWARFDie) -> None:
        """Visit a namespace"""
        namespace = Namespace(name=die.short_name)

        for attribute in die.attributes:
            match attribute.name:
                case "DW_AT_name":
                    pass
                case "DW_AT_export_symbols":
                    if namespace.name is not None:
                        namespace.is_inline = True
                case _:
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        self._cache[die.offset] = namespace

        for child in die.children:
            if child.tag == "DW_TAG_namespace":
                self.visit(child)
                member = self._cache[child.offset]
                assert member.parent is None, "Already has a parent"
                member.parent = namespace

            elif child.tag in {
                "DW_TAG_typedef",
                "DW_TAG_class_type",
                "DW_TAG_enumeration_type",
                "DW_TAG_union_type",
                "DW_TAG_structure_type",
                "DW_TAG_variable",
                "DW_TAG_subprogram",
                "DW_TAG_imported_module",
                "DW_TAG_imported_declaration",
            }:
                if not child.find("DW_AT_decl_file"):
                    continue

                decl_file = posixpath.abspath(child.decl_file.replace("\\", "/"))
                if not decl_file.startswith(self._base_dir):
                    continue

                decl_line = child.decl_line
                if not decl_line:
                    continue

                self.visit(child)
                if child.offset in self._cache:
                    member = self._cache[child.offset]
                    assert member.parent is None, "Already has a parent"
                    member.parent = namespace
                    self._add(decl_file, decl_line, member)
            else:
                raise ValueError(f"Unhandled child tag {child.tag}")

    def visit_typedef(self, die: DWARFDie) -> None:
        if not die.decl_file or not die.decl_line:
            return

        typedef = TypeDef(name=die.short_name)
        if type_attr := die.find("DW_AT_type"):
            type_die = type_attr.as_referenced_die()
            if type_die.short_name is None and type_die.tag in {
                "DW_TAG_class_type",
                "DW_TAG_union_type",
                "DW_TAG_enumeration_type",
                "DW_TAG_structure_type",
            }:
                # this is an in-place declaration
                self.visit(type_die)
                value = self._cache[type_die.offset]
                value.is_implicit = True
                typedef.value = value
            else:
                typedef.value = get_qualified_type(type_die)

        for attribute in die.attributes:
            if attribute.name in {
                "DW_AT_decl_file",
                "DW_AT_decl_line",
                "DW_AT_name",
                "DW_AT_type",
            }:
                continue

            match attribute.name:
                case "DW_AT_alignment":
                    typedef.alignment = attribute.value.as_constant()
                case _:
                    print(die.dump())
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        self._cache[die.offset] = typedef

    def visit_class_type(self, die: DWARFDie) -> None:
        self._handle_struct(
            die,
            Class(name=die.short_name),
        )

    def visit_enumeration_type(self, die: DWARFDie) -> None:
        enum = Enum(name=die.short_name)

        for attribute in die.attributes:
            if attribute.name in {
                "DW_AT_decl_file",
                "DW_AT_decl_line",
                "DW_AT_name",
                "DW_AT_byte_size",
                "DW_AT_declaration",
            }:
                continue

            match attribute.name:
                case "DW_AT_type":
                    enum.base = get_qualified_type(attribute.value.as_referenced_die())
                case "DW_AT_enum_class":
                    enum.is_class = True
                case _:
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        for child in die.children:
            match child.tag:
                case "DW_TAG_enumerator":
                    enum.values.append(
                        (
                            child.short_name,
                            child.find("DW_AT_const_value").as_constant(),
                        )
                    )
                case _:
                    raise ValueError(f"Unhandled child tag {child.tag}")

        self._cache[die.offset] = enum

    def visit_union_type(self, die: DWARFDie) -> None:
        self._handle_struct(
            die,
            Union(name=die.short_name),
        )

    def visit_structure_type(self, die: DWARFDie) -> None:
        self._handle_struct(
            die,
            Struct(name=die.short_name),
        )

    def visit_variable(self, die: DWARFDie) -> None:
        self._handle_attribute(die)

    def visit_member(self, die: DWARFDie) -> None:
        self._handle_attribute(die)

    def visit_subprogram(self, die: DWARFDie) -> None:
        if not die.find("DW_AT_decl_file") or not die.find("DW_AT_decl_line") or not die.short_name:
            return

        # If a type, variable, or function declared in a namespace is defined outside the body of the namespace
        # declaration, that type, variable, or function definition entry has a DW_AT_specification attribute whose
        # value is a reference to the debugging information entry representing the declaration of the type, variable
        # or function. Type, variable, or function entries with a DW_AT_specification attribute do not need to
        # duplicate information provided by the declaration entry referenced by the specification attribute.
        spec = die.find("DW_AT_specification")
        if spec is not None:
            spec = spec.as_referenced_die()
            assert spec.tag == "DW_TAG_subprogram", "Expected DW_TAG_subprogram for DW_AT_specification attribute."

            self.visit(spec)
            if spec.offset not in self._cache:
                return

            # this is a definition outside the body of the namespace, use fully qualified name
            printer = DWARFTypePrinter()
            printer.append_scopes(spec.parent)
            printer.append_unqualified_name(spec)
            name = str(printer)

            declaration = self._cache[spec.offset]
            function = Function(name=name, returns=declaration.returns)
        else:
            if die.find("DW_AT_artificial") is not None:
                return

            name = die.short_name
            returns = "void"
            if ret := die.find("DW_AT_type"):
                returns = get_qualified_type(ret.as_referenced_die())

            function = Function(name=name, returns=returns)

        for attribute in die.attributes:
            if attribute.name in {
                "DW_AT_linkage_name",
                "DW_AT_name",
                "DW_AT_decl_file",
                "DW_AT_decl_line",
                "DW_AT_low_pc",
                "DW_AT_high_pc",
                "DW_AT_frame_base",
                "DW_AT_call_all_calls",
                "DW_AT_calling_convention",
                "DW_AT_declaration",
                "DW_AT_prototyped",
                "DW_AT_artificial",
                "DW_AT_specification",
                "DW_AT_vtable_elem_location",
                "DW_AT_containing_type",  # used by llvm to link a vtable back to the type for which it was created
                "DW_AT_reference",
                "DW_AT_rvalue_reference",
                "DW_AT_external",
                "DW_AT_type",
            }:
                continue

            match attribute.name:
                case "DW_AT_inline":
                    function.is_inline = InlineAttribute(attribute.value.as_constant()) in {
                        InlineAttribute.DECLARED_NOT_INLINED,
                        InlineAttribute.DECLARED_INLINED,
                    }
                case "DW_AT_noreturn":
                    function.noreturn = True
                case "DW_AT_explicit":
                    function.is_explicit = True
                case "DW_AT_object_pointer":
                    pass
                    # object_pointer = attribute.value.as_referenced_die()
                    # print(object_pointer.dump())
                case "DW_AT_abstract_origin":
                    pass
                    # print(spec.dump())
                case "DW_AT_accessibility":
                    pass
                case "DW_AT_virtuality":
                    function.virtuality = VirtualityAttribute(attribute.value.as_constant())
                    assert function.virtuality != VirtualityAttribute.NONE, "Expected non-NONE virtuality"
                case "DW_AT_deleted":
                    function.is_deleted = True
                case _:
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        for child in die.children:
            if child.tag in {
                "DW_TAG_label",
                "DW_TAG_lexical_block",
                "DW_TAG_variable",
                "DW_TAG_inlined_subroutine",
                "DW_TAG_call_site",
                "DW_TAG_typedef",
                "DW_TAG_imported_module",
                "DW_TAG_imported_declaration",
                "DW_TAG_enumeration_type",
                "DW_TAG_class_type",
                "DW_TAG_structure_type",
                "DW_TAG_union_type",
            }:
                # TODO: handle local variables, types and functions
                continue

            match child.tag:
                case "DW_TAG_formal_parameter":
                    if child.find("DW_AT_artificial"):
                        continue  # ignore compiler-generated implicit parameters (e.g. vtt)

                    parameter = Parameter(
                        name=child.short_name,
                        type=get_qualified_type(child.find("DW_AT_type").as_referenced_die()),
                        kind=ParameterKind.POSITIONAL,
                    )
                    function.parameters.append(parameter)
                case "DW_TAG_unspecified_parameters":
                    parameter = Parameter(name="", type="", kind=ParameterKind.VARIADIC)
                    function.parameters.append(parameter)
                case (
                    "DW_TAG_template_type_parameter"
                    | "DW_TAG_template_value_parameter"
                    | "DW_TAG_GNU_template_parameter_pack"
                    | "DW_TAG_GNU_template_template_param"
                ):
                    # TODO: handle template params
                    pass
                case _:
                    raise ValueError(f"Unhandled child tag {child.tag}")

        if die.linkage_name or die.short_name:
            key = f"{die.linkage_name or die.short_name}@{len(function.parameters)}"
            self._functions[key].append(function)

            if key not in self._param_names:
                self._param_names[key] = [p.name for p in function.parameters]
            else:
                param_names = self._param_names[key]
                assert len(param_names) == len(function.parameters), "Parameter count mismatch"
                for i, param in enumerate(function.parameters):
                    if param_names[i] is None and param.name is not None:
                        param_names[i] = param.name

        self._cache[die.offset] = function

    def visit_imported_module(self, die: DWARFDie) -> None:
        if not die.decl_file or not die.decl_line:
            raise ValueError("No declaration file or line")

        import_die = die.find("DW_AT_import").as_referenced_die()
        assert import_die.tag == "DW_TAG_namespace", (
            f"Expected DW_TAG_namespace for DW_AT_import attribute. Got: {import_die.tag}"
        )
        self.visit(import_die)
        import_ = self._cache[import_die.offset]
        imported_module = ImportedModule(name="", import_=import_)

        for attribute in die.attributes:
            if attribute.name in {"DW_AT_decl_file", "DW_AT_decl_line", "DW_AT_import"}:
                continue

            raise ValueError(f"Unhandled attribute {attribute.name}")

        self._cache[die.offset] = imported_module

    def visit_imported_declaration(self, die: DWARFDie) -> None:
        if not die.decl_file or not die.decl_line:
            raise ValueError("No declaration file or line")

        import_die = die.find("DW_AT_import").as_referenced_die()
        self.visit(import_die)
        if import_die.tag == "DW_TAG_namespace":
            import_ = self._cache[import_die.offset]
            imported_decl = ImportedDeclaration(name=die.short_name, import_=import_)
        else:
            imported_decl = ImportedDeclaration(name="", import_=get_qualified_type(import_die))

        for attribute in die.attributes:
            if attribute.name in {
                "DW_AT_decl_file",
                "DW_AT_decl_line",
                "DW_AT_name",
                "DW_AT_import",
            }:
                continue

            raise ValueError(f"Unhandled attribute {attribute.name}")

        self._cache[die.offset] = imported_decl

    def generic_visit(self, die: DWARFDie) -> None:
        for child in die.children:
            self.visit(child)

    def _add(self, filepath: str, lineno: int, obj: Object) -> None:
        file = self._files[filepath]
        lines = file[lineno]

        if len(lines) > 4:
            # too many items on a single line (template instantiations?)
            return

        lines.append(obj)

    def _handle_attribute(self, die: DWARFDie) -> None:
        if not die.decl_file or not die.decl_line or not die.short_name:
            return

        if spec := die.find("DW_AT_specification"):
            spec = spec.as_referenced_die()
            assert spec.tag == "DW_TAG_member", "Expected DW_TAG_member"
            return

        variable = Attribute(name=die.short_name)

        for attribute in die.attributes:
            if attribute.name in {
                "DW_AT_decl_file",
                "DW_AT_decl_line",
                "DW_AT_name",
                "DW_AT_linkage_name",
                "DW_AT_external",
                "DW_AT_location",
                "DW_AT_declaration",
                "DW_AT_byte_size",
                "DW_AT_bit_offset",
                "DW_AT_specification",
            }:
                continue

            match attribute.name:
                case "DW_AT_type":
                    variable.type = get_qualified_type(attribute.value.as_referenced_die())
                case "DW_AT_const_value":
                    variable.default_value = attribute.value.as_constant()
                case "DW_AT_alignment":
                    variable.alignment = attribute.value.as_constant()
                case "DW_AT_accessibility":
                    pass  # TODO:
                case "DW_AT_data_member_location":
                    pass
                case "DW_AT_bit_size":
                    variable.bit_size = attribute.value.as_constant()
                case _:
                    print(die.dump())
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        for child in die.children:
            if child.tag in {
                "DW_TAG_template_type_parameter",
                "DW_TAG_template_value_parameter",
                "DW_TAG_GNU_template_parameter_pack",
            }:
                variable.is_template = True
                variable.default_value = None
            else:
                raise ValueError(f"Unhandled child tag {child.tag}")

        if variable.default_value is not None:
            int_value = variable.default_value
            if "float" in variable.type:
                variable.default_value = struct.unpack("f", struct.pack("I", int_value))[0]

        self._cache[die.offset] = variable

    def _handle_struct(self, die: DWARFDie, struct: Struct) -> None:
        class_name = struct.name.split("<", maxsplit=1)[0] if struct.name else None
        for attribute in die.attributes:
            if attribute.name in {
                "DW_AT_name",
                "DW_AT_decl_file",
                "DW_AT_decl_line",
                "DW_AT_calling_convention",
                "DW_AT_byte_size",  # TODO: this can be useful
                "DW_AT_declaration",
                "DW_AT_containing_type",
                "DW_AT_export_symbols",
            }:
                continue

            match attribute.name:
                case "DW_AT_alignment":
                    struct.alignment = attribute.value.as_constant()
                case "DW_AT_accessibility":
                    pass
                case _:
                    print(die.dump())
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        self._cache[die.offset] = struct

        for child in die.children:
            if child.tag in {
                "DW_TAG_typedef",
                "DW_TAG_class_type",
                "DW_TAG_enumeration_type",
                "DW_TAG_union_type",
                "DW_TAG_structure_type",
                "DW_TAG_member",
                "DW_TAG_subprogram",
                "DW_TAG_imported_module",
                "DW_TAG_imported_declaration",
            }:
                if not child.decl_line:
                    continue

                lines = struct.members[child.decl_line]
                if len(lines) > 4:
                    # too many items on a single line (template instantiations?)
                    continue

                self.visit(child)
                if child.offset not in self._cache:
                    continue

                member = self._cache[child.offset]
                lines.append(member)
                if isinstance(member, Function):
                    # constructors, destructors and operators should have no return type
                    if child.short_name.startswith("operator "):
                        member.returns = None
                    elif class_name and child.short_name:
                        function_name = child.short_name.split("<", maxsplit=1)[0]
                        if function_name == class_name or function_name == f"~{class_name}":
                            member.returns = None

                continue

            match child.tag:
                case (
                    "DW_TAG_template_type_parameter"
                    | "DW_TAG_template_value_parameter"
                    | "DW_TAG_GNU_template_parameter_pack"
                    | "DW_TAG_GNU_template_template_param"
                ):
                    # TODO: handle template params
                    pass
                case "DW_TAG_inheritance":
                    access = None
                    base = get_qualified_type(child.find("DW_AT_type").as_referenced_die())
                    for attribute in child.attributes:
                        if attribute.name in {
                            "DW_AT_type",
                            "DW_AT_data_member_location",
                        }:
                            continue

                        match attribute.name:
                            case "DW_AT_accessibility":
                                access = AccessAttribute(attribute.value.as_constant())
                            case "DW_AT_virtuality":
                                virtuality = VirtualityAttribute(attribute.value.as_constant())
                                assert virtuality != VirtualityAttribute.NONE, "Expected non-NONE virtuality"
                                base = "virtual " + base
                            case _:
                                raise ValueError(f"Unhandled attribute {attribute.name}")

                    struct.bases.append((base, access))

                case _:
                    raise ValueError(f"Unhandled child tag {child.tag}")
