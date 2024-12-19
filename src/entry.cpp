#include "entry.h"

#include <llvm/DebugInfo/DWARF/DWARFTypePrinter.h>

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

} // namespace dwarf2cpp
