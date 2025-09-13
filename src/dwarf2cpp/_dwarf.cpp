
#include <format>
#include <llvm/ADT/StringSwitch.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/DebugInfo/DWARF/DWARFTypePrinter.h>
#include <llvm/DebugInfo/DWARF/DWARFTypeUnit.h>
#include <llvm/Demangle/Demangle.h>
#include <pybind11/native_enum.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace {
std::string ToString(llvm::dwarf::Attribute attr) {
  static const std::unordered_map<llvm::dwarf::Attribute, std::string> map = {
#define HANDLE_DW_AT(ID, NAME, VERSION, VENDOR)                                \
  {llvm::dwarf::DW_AT_##NAME, "DW_AT_" #NAME},
#include "llvm/BinaryFormat/Dwarf.def"

#undef HANDLE_DW_AT
  };
  return map.at(attr);
}

llvm::dwarf::Attribute ToAttribute(const std::string &key) {
  static const std::unordered_map<std::string, llvm::dwarf::Attribute> map = {
#define HANDLE_DW_AT(ID, NAME, VERSION, VENDOR)                                \
  {"DW_AT_" #NAME, llvm::dwarf::DW_AT_##NAME},
#include "llvm/BinaryFormat/Dwarf.def"

#undef HANDLE_DW_AT
  };
  return map.at(key);
}
} // namespace

class PyDWARFContext {
public:
  explicit PyDWARFContext(const std::string &path) {
    auto result = llvm::object::ObjectFile::createObjectFile(path);
    if (!result) {
      throw std::runtime_error(toString(result.takeError()));
    }
    object_ = std::move(*result);
    context_ = llvm::DWARFContext::create(*object_.getBinary());
  }

  [[nodiscard]] auto info_section_units() const {
    std::vector<llvm::DWARFUnit *> units;
    for (const auto &unit : context_->info_section_units()) {
      units.push_back(unit.get());
    }
    return units;
  }
  [[nodiscard]] auto compile_units() const {
    std::vector<llvm::DWARFUnit *> units;
    for (const auto &unit : context_->compile_units()) {
      units.push_back(unit.get());
    }
    return units;
  }
  [[nodiscard]] auto getNumCompileUnits() const {
    return context_->getNumCompileUnits();
  }
  [[nodiscard]] auto getNumTypeUnits() const {
    return context_->getNumTypeUnits();
  }
  [[nodiscard]] auto getNumDWOCompileUnits() const {
    return context_->getNumDWOCompileUnits();
  }
  [[nodiscard]] auto getNumDWOTypeUnits() const {
    return context_->getNumDWOTypeUnits();
  }
  [[nodiscard]] auto getMaxVersion() const { return context_->getMaxVersion(); }
  [[nodiscard]] auto getMaxDWOVersion() const {
    return context_->getMaxDWOVersion();
  }
  [[nodiscard]] auto isLittleEndian() const {
    return context_->isLittleEndian();
  }
  [[nodiscard]] auto getCUAddrSize() const { return context_->getCUAddrSize(); }

private:
  llvm::object::OwningBinary<llvm::object::ObjectFile> object_;
  std::unique_ptr<llvm::DWARFContext> context_;
};

class PyDWARFTypePrinter {
public:
  PyDWARFTypePrinter() : os(buffer), printer(os) {}

  std::string string() {
    os.flush();
    return buffer;
  }

  auto appendQualifiedName(llvm::DWARFDie die) {
    printer.appendQualifiedName(die);
  }
  auto appendUnqualifiedName(llvm::DWARFDie die) {
    printer.appendUnqualifiedName(die);
  }
  auto appendScopes(llvm::DWARFDie die) { printer.appendScopes(die); }

private:
  std::string buffer;
  llvm::raw_string_ostream os;
  llvm::DWARFTypePrinter printer;
};

PYBIND11_MODULE(_dwarf, m) {
  py::native_enum<llvm::dwarf::AccessAttribute>(m, "AccessAttribute",
                                                "enum.IntEnum")
      .value("PUBLIC", llvm::dwarf::DW_ACCESS_public)
      .value("PROTECTED", llvm::dwarf::DW_ACCESS_protected)
      .value("PRIVATE", llvm::dwarf::DW_ACCESS_private)
      .finalize();

  py::native_enum<llvm::dwarf::VirtualityAttribute>(m, "VirtualityAttribute",
                                                    "enum.IntEnum")
      .value("NONE", llvm::dwarf::DW_VIRTUALITY_none)
      .value("VIRTUAL", llvm::dwarf::DW_VIRTUALITY_virtual)
      .value("PURE_VIRTUAL", llvm::dwarf::DW_VIRTUALITY_pure_virtual)
      .finalize();

  py::class_<PyDWARFContext>(m, "DWARFContext")
      .def(py::init<const std::string &>(), py::arg("path"))
      .def_property_readonly("info_section_units",
                             &PyDWARFContext::info_section_units,
                             py::return_value_policy::reference_internal)
      .def_property_readonly("compile_units", &PyDWARFContext::compile_units,
                             py::return_value_policy::reference_internal)
      .def_property_readonly("num_compile_units",
                             &PyDWARFContext::getNumCompileUnits)
      .def_property_readonly("num_type_units", &PyDWARFContext::getNumTypeUnits)
      .def_property_readonly("num_dwo_compile_units",
                             &PyDWARFContext::getNumDWOCompileUnits)
      .def_property_readonly("num_dwo_type_units",
                             &PyDWARFContext::getNumDWOTypeUnits)
      .def_property_readonly("max_version", &PyDWARFContext::getMaxVersion)
      .def_property_readonly("max_dwo_version",
                             &PyDWARFContext::getMaxDWOVersion)
      .def_property_readonly("is_little_endian",
                             &PyDWARFContext::isLittleEndian)
      .def_property_readonly("cu_addr_size", &PyDWARFContext::getCUAddrSize);

  py::class_<llvm::DWARFUnit>(m, "DWARFUnit")
      .def_property_readonly("length", &llvm::DWARFUnit::getLength)
      .def_property_readonly(
          "unit_die",
          [](llvm::DWARFUnit &self) -> std::optional<llvm::DWARFDie> {
            if (auto die = self.getUnitDIE(false); die.isValid()) {
              return die;
            }
            return std::nullopt;
          })
      .def_property_readonly("compilation_dir",
                             &llvm::DWARFUnit::getCompilationDir);

  py::class_<llvm::DWARFDie>(m, "DWARFDie")
      .def_property_readonly("offset", &llvm::DWARFDie::getOffset)
      .def_property_readonly("tag",
                             [](const llvm::DWARFDie &self) {
                               return TagString(self.getTag()).str();
                             })
      .def_property_readonly(
          "parent",
          [](const llvm::DWARFDie &self) -> std::optional<llvm::DWARFDie> {
            if (auto parent = self.getParent(); parent.isValid()) {
              return parent;
            }
            return std::nullopt;
          })
      .def_property_readonly("short_name", &llvm::DWARFDie::getShortName)
      .def_property_readonly("linkage_name", &llvm::DWARFDie::getLinkageName)
      .def_property_readonly("decl_line", &llvm::DWARFDie::getDeclLine)
      .def_property_readonly(
          "decl_file",
          [](const llvm::DWARFDie &self) -> std::optional<std::string> {
            if (auto form = self.findRecursively(llvm::dwarf::DW_AT_decl_file))
              return form->getAsFile(llvm::DILineInfoSpecifier::
                                         FileLineInfoKind::AbsoluteFilePath);
            return std::nullopt;
          })
      .def_property_readonly("attributes",
                             [](const llvm::DWARFDie &self) {
                               std::vector<llvm::DWARFAttribute> attrs;
                               for (const auto &attr : self.attributes()) {
                                 attrs.emplace_back(attr);
                               }
                               return attrs;
                             })
      .def_property_readonly("children",
                             [](const llvm::DWARFDie &self) {
                               std::vector<llvm::DWARFDie> children;
                               for (const auto &child : self.children()) {
                                 if (child.isValid()) {
                                   children.emplace_back(child);
                                 }
                               }
                               return children;
                             })
      .def("dump",
           [](const llvm::DWARFDie &self) {
             std::string result;
             llvm::raw_string_ostream os(result);
             self.dump(os);
             os.flush();
             return result;
           })
      .def("find",
           [](const llvm::DWARFDie &self, const std::string &attribute) {
             return self.find(ToAttribute(attribute));
           })
      .def("__hash__", &llvm::DWARFDie::getOffset)
      .def(py::self == py::self);

  py::class_<llvm::DWARFAttribute>(m, "DWARFAttribute")
      .def_readonly("offset", &llvm::DWARFAttribute::Offset)
      .def_readonly("byte_size", &llvm::DWARFAttribute::ByteSize)
      .def_property_readonly(
          "name",
          [](const llvm::DWARFAttribute &self) { return ToString(self.Attr); })
      .def_readonly("value", &llvm::DWARFAttribute::Value);

  py::class_<llvm::DWARFFormValue>(m, "DWARFFormValue")
      .def_property_readonly("form",
                             [](const llvm::DWARFFormValue &self) {
                               return FormEncodingString(self.getForm()).str();
                             })
      .def("as_referenced_die",
           [](const llvm::DWARFFormValue &self)
               -> std::optional<llvm::DWARFDie> {
             llvm::DWARFDie result;
             auto &V = self;
             auto *U = self.getUnit();
             if (std::optional<uint64_t> offset = V.getAsRelativeReference()) {
               result = const_cast<llvm::DWARFUnit *>(V.getUnit())
                            ->getDIEForOffset(V.getUnit()->getOffset() +
                                              offset.value());
             } else if (offset = V.getAsDebugInfoReference(); offset) {
               if (llvm::DWARFUnit *spec_unit =
                       U->getUnitVector().getUnitForOffset(offset.value()))
                 result = spec_unit->getDIEForOffset(*offset);
             } else if (std::optional<uint64_t> sig =
                            V.getAsSignatureReference()) {
               if (llvm::DWARFTypeUnit *TU = U->getContext().getTypeUnitForHash(
                       U->getVersion(), sig.value(), U->isDWOUnit()))
                 result =
                     TU->getDIEForOffset(TU->getTypeOffset() + TU->getOffset());
             }

             if (result.isValid()) {
               return result;
             }
             return std::nullopt;
           })
      .def("as_string",
           [](const llvm::DWARFFormValue &self) -> std::string {
             auto e = self.getAsCString();
             if (e) {
               return e.get();
             }
             throw py::value_error(toString(e.takeError()));
           })
      .def("as_constant", [](const llvm::DWARFFormValue &self) -> py::int_ {
        if (auto s = self.getAsSignedConstant(); s) {
          return s.value();
        }
        if (auto u = self.getAsUnsignedConstant(); u) {
          return u.value();
        }
        throw py::value_error("Invalid constant value");
      });

  py::class_<PyDWARFTypePrinter>(m, "DWARFTypePrinter")
      .def(py::init())
      .def("append_qualified_name", &PyDWARFTypePrinter::appendQualifiedName)
      .def("append_unqualified_name",
           &PyDWARFTypePrinter::appendUnqualifiedName)
      .def("append_scopes", &PyDWARFTypePrinter::appendScopes)
      .def("__str__", &PyDWARFTypePrinter::string);
}
