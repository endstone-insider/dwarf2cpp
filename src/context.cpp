#include "context.h"

#include <filesystem>

#include "posixpath.hpp"

namespace fs = std::filesystem;

namespace dwarf2cpp {

void Context::update(const llvm::DWARFDie &unit_die)
{
    if (!unit_die.isValid()) {
        return;
    }
    auto name = unit_die.getShortName();
    auto comp_dir = unit_die.getDwarfUnit()->getCompilationDir();
    if (!name || !comp_dir) {
        return;
    }

    auto base_dir = posixpath::commonpath({name, comp_dir});
    if (base_dir_.empty()) {
        base_dir_ = base_dir;
    }
    base_dir_ = posixpath::commonpath({base_dir, base_dir_});

    parse_children(unit_die);
}

std::string Context::base_dir() const
{
    return base_dir_;
}

const std::unordered_map<std::string, SourceFile> &Context::source_files() const
{
    return source_files_;
}

Entry *Context::get(const llvm::DWARFDie &index)
{
    const auto die = index.resolveTypeUnitReference();
    if (!die.find(llvm::dwarf::DW_AT_name) || !die.find(llvm::dwarf::DW_AT_decl_file) ||
        !die.find(llvm::dwarf::DW_AT_decl_line)) {
        return nullptr;
    }

    auto decl_file = die.getDeclFile(llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath);
    std::replace(decl_file.begin(), decl_file.end(), '\\', '/');
    decl_file = posixpath::normpath(decl_file);

    Entry *existing_entry = nullptr;
    if (const auto it = source_files_.find(decl_file); it != source_files_.end()) {
        existing_entry = it->second.get(die);
    }

    if (!existing_entry) {
        switch (die.getTag()) {
        case llvm::dwarf::DW_TAG_class_type:
        case llvm::dwarf::DW_TAG_enumeration_type:
        case llvm::dwarf::DW_TAG_structure_type:
        case llvm::dwarf::DW_TAG_typedef:
        case llvm::dwarf::DW_TAG_union_type:
        case llvm::dwarf::DW_TAG_subprogram:
            return source_files_[decl_file].add(die);
        default:
            return nullptr;
        }
    }
    return existing_entry;
}

void Context::parse_children(const llvm::DWARFDie &die) // NOLINT(*-no-recursion)
{
    for (const auto &child : die.children()) {
        if (child.getTag() == llvm::dwarf::DW_TAG_namespace) {
            parse_children(child);
            continue;
        }

        if (auto *entry = get(child); entry) {
            auto child_die = child.resolveTypeUnitReference();
            entry->parse(child_die);
            if (child_die != child) {
                entry->parse(child);
            }
        }
    }
}

} // namespace dwarf2cpp
