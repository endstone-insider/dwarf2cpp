#include "entry.h"

#include <sstream>

#include <llvm/DebugInfo/DWARF/DWARFTypePrinter.h>
#include <llvm/Demangle/Demangle.h>

#include "algorithm.hpp"

namespace {
std::string to_string(llvm::dwarf::AccessAttribute a)
{
    switch (a) {
    case llvm::dwarf::DW_ACCESS_public:
        return "public";
    case llvm::dwarf::DW_ACCESS_protected:
        return "protected";
    case llvm::dwarf::DW_ACCESS_private:
        return "private";
    default:
        throw std::runtime_error("unknown access");
    }
}
} // namespace

namespace dwarf2cpp {

void Entry::parse(const llvm::DWARFDie &die)
{
    if (auto attr = die.find(llvm::dwarf::DW_AT_accessibility); attr) {
        access_ = static_cast<llvm::dwarf::AccessAttribute>(attr->getAsUnsignedConstant().value());
    }
}

void Typedef::parse(const llvm::DWARFDie &die)
{
    Entry::parse(die);
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
    Entry::parse(die);
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
    Entry::parse(die);
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

void Enum::parse(const llvm::DWARFDie &die)
{
    Entry::parse(die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto type = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type); type.isValid()) {
        std::string base_type;
        type = type.resolveTypeUnitReference();
        llvm::raw_string_ostream os(base_type);
        llvm::DWARFTypePrinter type_printer(os);
        type_printer.appendQualifiedName(type);
        base_type_ = base_type;
    }
    for (const auto &child : die.children()) {
        if (child.getTag() != llvm::dwarf::DW_TAG_enumerator) {
            continue;
        }
        Enumerator enumerator;
        enumerator.name = child.getShortName();
        enumerator.value = child.find(llvm::dwarf::DW_AT_const_value)->getAsSignedConstant().value();
        enumerators_.push_back(enumerator);
    }
}

std::string Enum::to_source() const
{
    std::stringstream ss;
    ss << "enum ";
    if (!name_.empty()) {
        ss << "class " << name_;
    }
    if (!base_type_.has_value()) {
        ss << " : " << base_type_.value();
    }
    ss << " {\n";
    for (const auto &[name, value] : enumerators_) {
        ss << "    " << name << " = " << value << ",\n";
    }
    ss << "};";
    return ss.str();
}

void Struct::parse(const llvm::DWARFDie &die)
{
    Entry::parse(die);
    if (auto *buffer = die.getShortName(); buffer) {
        name_ = buffer;
    }
    if (auto attr = die.find(llvm::dwarf::DW_AT_byte_size); attr) {
        byte_size = attr->getAsUnsignedConstant().value();
    }
    for (const auto &child : die.children()) {
        std::unique_ptr<Entry> entry;
        std::size_t decl_line = child.getDeclLine();

        switch (child.getTag()) {
        case llvm::dwarf::DW_TAG_inheritance: {
            auto access = is_class_ ? llvm::dwarf::DW_ACCESS_private : llvm::dwarf::DW_ACCESS_public;
            if (auto attr = child.find(llvm::dwarf::DW_AT_accessibility)) {
                access = static_cast<llvm::dwarf::AccessAttribute>(attr->getAsUnsignedConstant().value());
            }
            auto type = child.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_type);
            type = type.resolveTypeUnitReference();
            std::string base_type;
            llvm::raw_string_ostream os(base_type);
            llvm::DWARFTypePrinter type_printer(os);
            type_printer.appendQualifiedName(type);
            base_classes_.emplace_back(access, base_type);
            break;
        }
        // case llvm::dwarf::DW_TAG_member:
        //     entry = std::make_unique<Field>();
        //     break;
        case llvm::dwarf::DW_TAG_subprogram:
            entry = std::make_unique<Function>(true);
            break;
        // case llvm::dwarf::DW_TAG_union_type:
        //     entry = std::make_unique<Union>();
        //     break;
        case llvm::dwarf::DW_TAG_structure_type:
            entry = std::make_unique<Struct>(false);
            break;
        case llvm::dwarf::DW_TAG_class_type:
            entry = std::make_unique<Struct>(true);
            break;
        case llvm::dwarf::DW_TAG_enumeration_type:
            entry = std::make_unique<Enum>();
            break;
        case llvm::dwarf::DW_TAG_typedef:
            entry = std::make_unique<Typedef>();
            break;
        default:
            break;
        }

        if (entry && decl_line > 0) {
            entry->parse(child);
            members_[decl_line].emplace_back(std::move(entry));
        }
    }
}

std::string Struct::to_source() const
{
    auto default_access = is_class_ ? llvm::dwarf::DW_ACCESS_private : llvm::dwarf::DW_ACCESS_public;

    std::stringstream ss;
    ss << (is_class_ ? "class " : "struct ") << name_;
    if (!base_classes_.empty()) {
        ss << ": ";
        for (auto i = 0; i < base_classes_.size(); ++i) {
            if (i > 0) {
                ss << ", ";
            }
            const auto &[access, base] = base_classes_[i];
            if (access != default_access) {
                ss << to_string(access) << " ";
            }
            ss << base;
        }
    }
    ss << " {\n";

    auto last_access = default_access;
    for (const auto &[decl_line, member] : members_) {
        for (const auto &m : member) {
            if (auto current_access = m->access().value_or(default_access); current_access != last_access) {
                ss << to_string(current_access) << ":\n";
                last_access = current_access;
            }

            std::stringstream is(m->to_source());
            std::string line;
            while (std::getline(is, line)) {
                ss << "    " << line << "\n";
            }
        }
    }
    ss << "};";
    if (!name_.empty() && byte_size.has_value()) {
        ss << "\n";
        ss << "static_assert(sizeof(" << name_ << ") == " << byte_size.value() << ");\n";
    }
    return ss.str();
}

} // namespace dwarf2cpp
