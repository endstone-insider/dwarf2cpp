import copy
import logging
import posixpath
import struct
from collections import defaultdict
from typing import Any, Callable, Generator

from tqdm import tqdm

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
    Template,
    TemplateParameter,
    TemplateParameterKind,
    TypeDef,
    Union,
)

logger = logging.getLogger("dwarf2cpp")


def float_to_str(value: float) -> str:
    s = f"{value:.7f}".rstrip("0")
    if s[-1] == ".":
        s += "0"
    return s


def double_to_str(value: float) -> str:
    s = f"{value:.16f}".rstrip("0")
    if s[-1] == ".":
        s += "0"
    return s


class Visitor:
    """
    Visitor iterators on compile units to extract data from them.
    """

    def __init__(self, context: DWARFContext, base_dir: str):
        self.context = context
        self._files: dict[str, dict[int, list[Object]]] = defaultdict(lambda: defaultdict(list))
        self._base_dir = base_dir
        self._objects = {}
        self._param_names: dict[str, list[str]] = {}
        self._functions: dict[str, list[Function]] = defaultdict(list)
        self._templates: dict[str | int, dict[int, list[Template]]] = defaultdict(lambda: defaultdict(list))
        self._types = {}

    @property
    def files(self) -> Generator[tuple[str, dict[int, list[Object]]], None, None]:
        """Build and return the files attached to this visitor.

        This method triggers a complete visit of the compile units.

        Returns:
            List of files
        """
        for i, tu in tqdm(
            enumerate(self.context.types_section_units),
            desc="Visiting type units",
            total=self.context.num_type_units,
            bar_format="[{n_fmt}/{total_fmt}] {desc} [{elapsed}, {rate_fmt}]",
        ):
            tu_die = tu.unit_die
            self.visit(tu_die)

        for i, cu in (
            pbar := tqdm(
                enumerate(self.context.compile_units),
                total=self.context.num_compile_units,
                bar_format="[{n_fmt}/{total_fmt}] {desc}",
            )
        ):
            cu_die = cu.unit_die
            compilation_dir = cu.compilation_dir.replace("\\", "/")

            if not compilation_dir.startswith(self._base_dir):
                pbar.set_description_str(f"Skipping compile unit {compilation_dir}")
                continue

            rel_path = posixpath.relpath(cu_die.short_name, self._base_dir)
            pbar.set_description_str(f"Visiting compile unit {rel_path}")
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
                result = []

                for item in objects:
                    if not result:
                        # First item, just add it
                        result.append(item)
                    elif item not in result:
                        last = result[-1]
                        if not last.merge(item):
                            # merge() returned False, so append new item
                            result.append(item)

                file[line] = result

            yield rel_path, file

    def visit(self, die: DWARFDie) -> None:
        if self._get(die):
            return

        kind = die.tag.split("DW_TAG_", maxsplit=1)[1]
        func: Callable[[DWARFDie], None] = getattr(self, f"visit_{kind}", self.generic_visit)
        func(die)

    def visit_compile_unit(self, die: DWARFDie):
        self._handle_unit(die)

    def visit_type_unit(self, die: DWARFDie):
        self._handle_unit(die)

    def _handle_unit(self, die: DWARFDie):
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
                decl_file = child.decl_file
                if not decl_file:
                    continue

                decl_file = posixpath.normpath(decl_file.replace("\\", "/"))
                if not decl_file.startswith(self._base_dir):
                    continue

                decl_line = child.decl_line
                if not decl_line:
                    continue

                self.visit(child)
                if obj := self._get(child):
                    if template := obj.template:
                        if template := self._register_template(decl_file, decl_line, template):
                            self._add(decl_file, decl_line, template)

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

        self._set(die, namespace)

        for child in die.children:
            if child.tag == "DW_TAG_namespace":
                self.visit(child)
                member = self._get(child)
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
                decl_file = child.decl_file
                if not decl_file:
                    continue

                decl_file = posixpath.normpath(decl_file.replace("\\", "/"))
                if not decl_file.startswith(self._base_dir):
                    continue

                decl_line = child.decl_line
                if not decl_line:
                    continue

                self.visit(child)
                if member := self._get(child):
                    assert member.parent is None, "Already has a parent"
                    member.parent = namespace
                    if template := member.template:
                        template.parent = namespace
                        if template := self._register_template(decl_file, decl_line, template):
                            self._add(decl_file, decl_line, template)

                    self._add(decl_file, decl_line, member)
            else:
                raise ValueError(f"Unhandled child tag {child.tag}")

    def visit_typedef(self, die: DWARFDie) -> None:
        if not die.decl_file or not die.decl_line:
            return

        typedef = TypeDef(name=die.short_name)
        if type_attr := die.find("DW_AT_type"):
            type_die = type_attr.as_referenced_die().resolve_type_unit_reference()
            if type_die.short_name is None and type_die.tag in {
                "DW_TAG_class_type",
                "DW_TAG_union_type",
                "DW_TAG_enumeration_type",
                "DW_TAG_structure_type",
            }:
                # this is an in-place declaration
                self.visit(type_die)
                value = self._get(type_die)
                value.is_implicit = True
                typedef.value = value
            else:
                typedef.value = self._resolve_type(type_die)

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

        self._set(die, typedef)

    def visit_class_type(self, die: DWARFDie) -> None:
        self._handle_struct(
            die,
            Class(name=die.short_name),
        )
        self.visit(die.resolve_type_unit_reference())

    def visit_enumeration_type(self, die: DWARFDie) -> None:
        enum = Enum(name=die.short_name)

        for attribute in die.attributes:
            if attribute.name in {
                "DW_AT_decl_file",
                "DW_AT_decl_line",
                "DW_AT_name",
                "DW_AT_byte_size",
                "DW_AT_declaration",
                "DW_AT_signature",
            }:
                continue

            match attribute.name:
                case "DW_AT_type":
                    enum.base = self._resolve_type(attribute.value.as_referenced_die())
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

        self._set(die, enum)
        self.visit(die.resolve_type_unit_reference())

    def visit_union_type(self, die: DWARFDie) -> None:
        self._handle_struct(
            die,
            Union(name=die.short_name),
        )
        self.visit(die.resolve_type_unit_reference())

    def visit_structure_type(self, die: DWARFDie) -> None:
        self._handle_struct(
            die,
            Struct(name=die.short_name),
        )
        self.visit(die.resolve_type_unit_reference())

    def visit_variable(self, die: DWARFDie) -> None:
        self._handle_attribute(die)

    def visit_member(self, die: DWARFDie) -> None:
        self._handle_attribute(die)

        member = self._get(die)
        if member and die.find("DW_AT_external"):
            member.is_static = True

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
            declaration = self._get(spec)
            if not declaration:
                return

            op = die.find("DW_AT_object_pointer")
            if not op:
                # check if we have object pointer (i.e., this), if not, then this is a static member function
                declaration.is_static = True
            else:
                op = op.as_referenced_die()
                t = op.find("DW_AT_type")
                while t:
                    t = t.as_referenced_die().resolve_type_unit_reference()
                    if t.tag == "DW_TAG_const_type":
                        declaration.is_const = True
                        break
                    t = t.find("DW_AT_type")

            # this is a definition outside the body of the namespace, use fully qualified name
            printer = DWARFTypePrinter()
            printer.append_scopes(spec.parent)
            printer.append_unqualified_name(spec)
            name = str(printer)

            function = Function(name=name, returns=declaration.returns, is_const=declaration.is_const)
        else:
            if die.find("DW_AT_artificial") is not None:
                return

            name = die.short_name
            returns = "void"
            if ret := die.find("DW_AT_type"):
                returns = self._resolve_type(ret.as_referenced_die())

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
                "DW_AT_GNU_all_call_sites",
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
                case "DW_AT_abstract_origin":
                    pass
                case "DW_AT_accessibility":
                    function.access = AccessAttribute(attribute.value.as_constant())
                case "DW_AT_virtuality":
                    function.virtuality = VirtualityAttribute(attribute.value.as_constant())
                    assert function.virtuality != VirtualityAttribute.NONE, "Expected non-NONE virtuality"
                case "DW_AT_deleted":
                    function.is_deleted = True
                case _:
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        template_params = []
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
                "DW_TAG_GNU_call_site",
            }:
                # TODO: handle local variables, types and functions
                continue

            if child.tag in {
                "DW_TAG_template_type_parameter",
                "DW_TAG_template_value_parameter",
                "DW_TAG_GNU_template_parameter_pack",
                "DW_TAG_GNU_template_template_param",
            }:
                template_params.append(child)
                continue

            match child.tag:
                case "DW_TAG_formal_parameter":
                    if child.find("DW_AT_artificial"):
                        continue  # ignore compiler-generated implicit parameters (e.g. vtt)

                    parameter = Parameter(
                        name=child.short_name,
                        type=self._resolve_type(child.find("DW_AT_type").as_referenced_die(), split=True),
                        kind=ParameterKind.POSITIONAL,
                    )
                    function.parameters.append(parameter)
                case "DW_TAG_unspecified_parameters":
                    parameter = Parameter(name="", type="", kind=ParameterKind.VARIADIC)
                    function.parameters.append(parameter)
                case _:
                    raise ValueError(f"Unhandled child tag {child.tag}")

        # sync parameter names from definition to declaration
        key = None
        if die.linkage_name:
            # c++ functions with external linkage
            key = f"{die.linkage_name}@{len(function.parameters)}"
        elif die.find("DW_AT_external") and die.short_name:
            # c functions with external linkage
            key = f"{die.short_name}@{len(function.parameters)}"

        if key:
            self._functions[key].append(function)
            # if this is a definition of a class constructor, it has a DW_AT_specification pointing to the
            # declaration with no DW_AT_linkage_name. Since we know the relationship, we can manually add the
            # declaration to the function map
            if spec and not spec.linkage_name:
                self._functions[key].append(self._get(spec))

            if key not in self._param_names:
                self._param_names[key] = [p.name for p in function.parameters]
            else:
                param_names = self._param_names[key]
                assert len(param_names) == len(function.parameters), "Parameter count mismatch"
                for i, param in enumerate(function.parameters):
                    if param_names[i] is None and param.name is not None:
                        param_names[i] = param.name

        if template_params:
            function.template = Template(name="")  # without declaration as there is no trivial way to infer that
            for template_param in template_params:
                self.visit(template_param)
                function.template.parameters.append(self._get(template_param))

        self._set(die, function)

    def visit_imported_module(self, die: DWARFDie) -> None:
        if not die.decl_file or not die.decl_line:
            raise ValueError("No declaration file or line")

        import_die = die.find("DW_AT_import").as_referenced_die()
        assert import_die.tag == "DW_TAG_namespace", (
            f"Expected DW_TAG_namespace for DW_AT_import attribute. Got: {import_die.tag}"
        )
        self.visit(import_die)
        import_ = self._get(import_die)
        imported_module = ImportedModule(name="", import_=import_)

        for attribute in die.attributes:
            if attribute.name in {"DW_AT_decl_file", "DW_AT_decl_line", "DW_AT_import"}:
                continue

            raise ValueError(f"Unhandled attribute {attribute.name}")

        self._set(die, imported_module)

    def visit_imported_declaration(self, die: DWARFDie) -> None:
        if not die.decl_file or not die.decl_line:
            raise ValueError("No declaration file or line")

        import_die = die.find("DW_AT_import").as_referenced_die()
        self.visit(import_die)
        import_ = self._get(import_die)

        if not import_:
            return

        if import_die.tag == "DW_TAG_namespace":
            imported_decl = ImportedDeclaration(name=die.short_name, import_=import_)
        else:
            imported_decl = ImportedDeclaration(name="", import_=self._resolve_type(import_die))

        assert imported_decl.import_ is not None, "Expected valid import."

        for attribute in die.attributes:
            if attribute.name in {
                "DW_AT_decl_file",
                "DW_AT_decl_line",
                "DW_AT_name",
                "DW_AT_import",
            }:
                continue

            raise ValueError(f"Unhandled attribute {attribute.name}")

        self._set(die, imported_decl)

    def visit_template_type_parameter(self, die: DWARFDie) -> None:
        param = TemplateParameter(TemplateParameterKind.TYPE, name=die.short_name)

        # A template type parameter entry has a DW_AT_type attribute
        # describing the actual type by which the formal is replaced.
        if ty := die.find("DW_AT_type"):
            param.type = self._resolve_type(ty.as_referenced_die())
        else:
            param.type = "void"

        for attribute in die.attributes:
            if attribute.name in {"DW_AT_name", "DW_AT_type"}:
                continue

            match attribute.name:
                case "DW_AT_default_value":
                    param.default = param.type
                case _:
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        for child in die.children:
            raise ValueError(f"Unhandled child tag {child.tag}")

        self._set(die, param)

    def visit_template_value_parameter(self, die: DWARFDie) -> None:
        param = TemplateParameter(TemplateParameterKind.CONSTANT, name=die.short_name)
        param.type = self._resolve_type(die.find("DW_AT_type").as_referenced_die())

        if value := die.find("DW_AT_const_value"):
            param.value = value.as_constant()

        for attribute in die.attributes:
            if attribute.name in {"DW_AT_name", "DW_AT_type", "DW_AT_const_value", "DW_AT_location"}:
                continue

            match attribute.name:
                case "DW_AT_default_value":
                    param.default = param.value
                case _:
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        for child in die.children:
            raise ValueError(f"Unhandled child tag {child.tag}")

        self._set(die, param)

    def visit_GNU_template_parameter_pack(self, die: DWARFDie) -> None:
        param = TemplateParameter(TemplateParameterKind.PACK, name=die.short_name)

        for attribute in die.attributes:
            if attribute.name in {"DW_AT_name"}:
                continue

            match attribute.name:
                case _:
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        parameters = []
        for child in die.children:
            if child.tag == "DW_TAG_template_type_parameter":
                self.visit(child)
                p = self._get(child)
                parameters.append(p)
            elif child.tag == "DW_TAG_template_value_parameter":
                self.visit(child)
                p = self._get(child)
                parameters.append(p)
            else:
                raise ValueError(f"Unhandled child tag {child.tag}")

        if parameters:
            assert all(p.kind == parameters[0].kind for p in parameters), "Parameter kind mismatch"
            if all(p.value is not None for p in parameters):
                if all(p.type == parameters[0].type for p in parameters):
                    param.type = parameters[0].type
                else:
                    param.type = "auto"

            param.parameters = parameters

        self._set(die, param)

    def visit_GNU_template_template_param(self, die: DWARFDie) -> None:
        param = TemplateParameter(TemplateParameterKind.TEMPLATE, name=die.short_name)

        for attribute in die.attributes:
            if attribute.name in {"DW_AT_name"}:
                continue

            match attribute.name:
                case "DW_AT_GNU_template_name":
                    param.type = attribute.value.as_string()
                case _:
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        for child in die.children:
            raise ValueError(f"Unhandled child tag {child.tag}")

        self._set(die, param)

    def generic_visit(self, die: DWARFDie) -> None:
        for child in die.children:
            self.visit(child)

    def _add(self, filepath: str, lineno: int, obj: Object) -> None:
        file = self._files[filepath]
        lines = file[lineno]

        if len(lines) >= 8:
            # too many items on a single line (template instantiations?)
            return

        lines.append(obj)

    def _register_template(self, key: str | int, lineno: int, template: Template) -> Template | None:
        if not template.declaration:
            return None

        templates = self._templates[key][lineno]

        # make a copy for template declaration
        template = copy.copy(template)
        template.parameters = [p.to_declaration() for p in template.parameters]

        # try to merge with existing templates
        for t in templates:
            if t.merge(template):
                return None

        templates.append(template)
        return template

    def _handle_attribute(self, die: DWARFDie) -> None:
        if not die.decl_file or not die.decl_line or not die.short_name:
            return

        if spec := die.find("DW_AT_specification"):
            spec = spec.as_referenced_die()
            assert spec.tag == "DW_TAG_member", "Expected DW_TAG_member"
            return

        variable = Attribute(name=die.short_name)
        ty = die.find("DW_AT_type")
        ty = ty.as_referenced_die().resolve_type_unit_reference()
        if ty.short_name is None and ty.tag in {
            "DW_TAG_class_type",
            "DW_TAG_union_type",
            "DW_TAG_enumeration_type",
            "DW_TAG_structure_type",
        }:
            # this is an in-place declaration
            self.visit(ty)
            value = self._get(ty)
            value.is_implicit = True
            variable.type = value
        else:
            variable.type = self._resolve_type(ty, split=True)

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
                "DW_AT_type",
            }:
                continue

            match attribute.name:
                case "DW_AT_const_value":
                    int_value = attribute.value.as_constant()
                    t = variable.type[0]
                    if t.endswith("float"):
                        variable.default_value = float_to_str(struct.unpack("f", struct.pack("I", int_value))[0])
                    elif t.endswith("double"):
                        variable.default_value = double_to_str(struct.unpack("d", struct.pack("Q", int_value))[0])
                    elif t.endswith("bool"):
                        variable.default_value = "true" if bool(int_value) else "false"
                    else:
                        variable.default_value = int_value

                case "DW_AT_alignment":
                    variable.alignment = attribute.value.as_constant()
                case "DW_AT_accessibility":
                    variable.access = AccessAttribute(attribute.value.as_constant())
                case "DW_AT_data_member_location":
                    pass
                case "DW_AT_bit_size":
                    variable.bit_size = attribute.value.as_constant()
                case _:
                    print(die.dump())
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        template_params: list[DWARFDie] = []
        for child in die.children:
            if child.tag in {
                "DW_TAG_template_type_parameter",
                "DW_TAG_template_value_parameter",
                "DW_TAG_GNU_template_parameter_pack",
            }:
                template_params.append(child)
            else:
                raise ValueError(f"Unhandled child tag {child.tag}")

        if template_params:
            declaration = copy.copy(variable)
            assert isinstance(declaration.type, tuple)
            declaration.type = (declaration.type[0].split("<", maxsplit=1)[0], declaration.type[1])
            declaration.default_value = None
            declaration.is_declaration = True

            variable.template = Template(name="", declaration=declaration)
            for template_param in template_params:
                self.visit(template_param)
                variable.template.parameters.append(self._get(template_param))

        self._set(die, variable)

    def _handle_struct(self, die: DWARFDie, struct: Struct) -> None:
        class_name = struct.name.split("<", maxsplit=1)[0] if struct.name else None
        access = AccessAttribute.PRIVATE if isinstance(struct, Class) else AccessAttribute.PUBLIC

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
                "DW_AT_signature",  # used by DWARFv4, removed in DWARFv5
            }:
                continue

            match attribute.name:
                case "DW_AT_alignment":
                    struct.alignment = attribute.value.as_constant()
                case "DW_AT_accessibility":
                    struct.access = AccessAttribute(attribute.value.as_constant())
                case _:
                    print(die.dump())
                    raise ValueError(f"Unhandled attribute {attribute.name}")

        self._set(die, struct)

        template_params = []
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
                if self._get(child) is None:
                    continue

                member = self._get(child)
                member.parent = struct

                # If no accessibility attribute is present, private access is assumed for members of a
                # class and public access is assumed for members of a structure, union, or interface
                if member.access is None:
                    member.access = access

                if template := member.template:
                    template.parent = struct
                    if template.access is None:
                        template.access = access

                    if template := self._register_template(die.offset, child.decl_line, template):
                        lines.append(template)

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

            if child.tag in {
                "DW_TAG_template_type_parameter",
                "DW_TAG_template_value_parameter",
                "DW_TAG_GNU_template_parameter_pack",
                "DW_TAG_GNU_template_template_param",
            }:
                template_params.append(child)
                continue

            match child.tag:
                case "DW_TAG_inheritance":
                    inherit_access = None
                    base = self._resolve_type(child.find("DW_AT_type").as_referenced_die())
                    for attribute in child.attributes:
                        if attribute.name in {
                            "DW_AT_type",
                            "DW_AT_data_member_location",
                        }:
                            continue

                        match attribute.name:
                            case "DW_AT_accessibility":
                                inherit_access = AccessAttribute(attribute.value.as_constant())
                            case "DW_AT_virtuality":
                                virtuality = VirtualityAttribute(attribute.value.as_constant())
                                assert virtuality != VirtualityAttribute.NONE, "Expected non-NONE virtuality"
                                base = "virtual " + base
                            case _:
                                raise ValueError(f"Unhandled attribute {attribute.name}")

                    struct.bases.append((base, inherit_access))

                case _:
                    raise ValueError(f"Unhandled child tag {child.tag}")

        if template_params:
            declaration = copy.copy(struct)
            declaration.name = declaration.name.split("<", maxsplit=1)[0]
            declaration.bases = []
            declaration.members = {}
            declaration.alignment = None
            declaration.is_declaration = True

            struct.template = Template(name="", declaration=declaration)
            for template_param in template_params:
                self.visit(template_param)
                struct.template.parameters.append(self._get(template_param))

    def _get(self, die: DWARFDie) -> Any | None:
        key = (die.unit.is_type_unit, die.offset)
        return self._objects.get(key, None)

    def _set(self, die: DWARFDie, obj) -> None:
        key = (die.unit.is_type_unit, die.offset)
        assert key not in self._objects
        self._objects[key] = obj

    def _resolve_type(self, die: DWARFDie, split=False) -> str | tuple[str, str]:
        die = die.resolve_type_unit_reference()

        key = (die.unit.is_type_unit, die.offset, split)
        if key in self._types:
            return self._types[key]

        printer = DWARFTypePrinter()
        if not split:
            printer.append_qualified_name(die)
            ty = str(printer).strip()
            self._types[key] = ty
        else:
            inner = printer.append_qualified_name_before(die)
            before = str(printer).strip()

            printer = DWARFTypePrinter()
            printer.append_unqualified_name_after(die, inner)
            after = str(printer).strip()
            self._types[key] = (before, after)

        return self._types[key]
