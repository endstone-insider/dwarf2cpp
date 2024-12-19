#include "entry.h"

#include <sstream>

#include <llvm/DebugInfo/DWARF/DWARFTypePrinter.h>
#include <llvm/Demangle/Demangle.h>

#include "algorithm.hpp"

namespace dwarf2cpp {

void Typedef::parse(const llvm::DWARFDie &die)
{
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        type = type.resolveTypeUnitReference();
        llvm::raw_string_ostream os(type_);
        llvm::DWARFTypePrinter type_printer(os);
        type_printer.appendQualifiedName(type);
    }
}

std::string Typedef::to_source() const
{
    return "using " + name_ + " = " + type_ + ";";
}

void Parameter::parse(const llvm::DWARFDie &die)
{
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        type = type.resolveTypeUnitReference();
        llvm::raw_string_ostream os(type_);
        llvm::DWARFTypePrinter type_printer(os);
        type_printer.appendQualifiedName(type);
    }
}

std::string Parameter::to_source() const
{
    return type_ + " " + name_;
}

void Function::parse(const llvm::DWARFDie &die)
{
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto *buffer = die.getLinkageName(); buffer) {
        linkage_name_ = buffer;
        if (const char *demangled = llvm::itaniumDemangle(linkage_name_, true); demangled) {
            std::string demangled_name = demangled;
            is_const_ = endswith(demangled_name, "const");
        }
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        type = type.resolveTypeUnitReference();
        llvm::raw_string_ostream os(return_type_);
        llvm::DWARFTypePrinter type_printer(os);
        type_printer.appendQualifiedName(type);
    }
    else {
        return_type_ = "void";
    }
    if (die.find(llvm::dwarf::DW_AT_explicit)) {
        is_explicit_ = true;
    }
    if (die.find(llvm::dwarf::DW_AT_object_pointer)) {
        is_static_ = false;
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_virtuality); attr.has_value()) {
        virtuality_ = static_cast<llvm::dwarf::VirtualityAttribute>(attr->getAsUnsignedConstant().value());
    }
    for (const auto &child : die.children()) {
        switch (child.getTag()) {
        case llvm::dwarf::DW_TAG_formal_parameter: {
            auto &param = parameters_.emplace_back();
            param.parse(child);
        }
        default:
            // TODO: template params
            break;
        }
    }
}

std::string Function::to_source() const
{
    std::stringstream ss;
    if (is_member_ && is_static_) {
        ss << "static ";
    }
    if (virtuality_ > llvm::dwarf::DW_VIRTUALITY_none) {
        ss << "virtual ";
    }
    if (!startswith(name_, "operator ")) {
        ss << return_type_ << " ";
    }
    if (is_explicit_) {
        ss << "explicit ";
    }
    ss << name_ << "(";
    for (auto it = parameters_.begin(); it != parameters_.end(); ++it) {
        ss << it->to_source();
        if (it < parameters_.end() - 1) {
            ss << ", ";
        }
    }
    ss << ")";
    if (is_const_) {
        ss << " const";
    }
    if (virtuality_ == llvm::dwarf::DW_VIRTUALITY_pure_virtual) {
        ss << " = 0";
    }
    ss << ";";
    return ss.str();
}

} // namespace dwarf2cpp
