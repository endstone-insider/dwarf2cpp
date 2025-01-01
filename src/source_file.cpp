#include "source_file.h"

#include <sstream>
#include <utility>

namespace dwarf2cpp {

Entry *SourceFile::add(const llvm::DWARFDie &die)
{
    std::size_t decl_line = die.getDeclLine();
    std::string short_name = die.getShortName();

    if (get(die)) {
        throw std::runtime_error("Duplicate declaration");
    }

    if (auto it = lines_.find(decl_line); it != lines_.end()) {
        if (it->second.size() > 16) { // TODO: avoid magic number?
            return nullptr;
        }
    }

    std::unique_ptr<Entry> entry;
    switch (die.getTag()) {
    case llvm::dwarf::DW_TAG_class_type:
        entry = std::make_unique<StructLike>(StructLike::Kind::Class);
        break;
    case llvm::dwarf::DW_TAG_enumeration_type:
        entry = std::make_unique<Enum>();
        break;
    case llvm::dwarf::DW_TAG_structure_type:
        entry = std::make_unique<StructLike>(StructLike::Kind::Struct);
        break;
    case llvm::dwarf::DW_TAG_typedef:
        entry = std::make_unique<Typedef>();
        break;
    case llvm::dwarf::DW_TAG_union_type:
        entry = std::make_unique<StructLike>(StructLike::Kind::Union);
        break;
    case llvm::dwarf::DW_TAG_subprogram:
        entry = std::make_unique<Function>(false);
        break;
    default:
        return nullptr;
    }

    const auto &ref = lines_[decl_line].emplace_back(std::move(entry));
    lookup_map_[decl_line][short_name] = ref.get();
    return ref.get();
}

Entry *SourceFile::get(const llvm::DWARFDie &die)
{
    std::size_t decl_line = die.getDeclLine();
    std::string short_name = die.getShortName();
    if (lookup_map_.find(decl_line) == lookup_map_.end()) {
        return nullptr;
    }
    if (lookup_map_[decl_line].find(short_name) == lookup_map_[decl_line].end()) {
        return nullptr;
    }
    return lookup_map_[decl_line][short_name];
}

std::string SourceFile::to_source() const
{
    std::stringstream ss;
    std::vector<std::string> prev_ns;
    bool first_line = true;
    for (const auto &[line, entries] : lines_) {
        if (!first_line) {
            ss << "\n";
        }

        for (const auto &entry : entries) {
            // Get current namespaces
            const auto &current_ns = entry->namespaces();

            // Find the point of divergence between previous and current namespaces
            size_t level = 0;
            while (level < prev_ns.size() && level < current_ns.size() && prev_ns[level] == current_ns[level]) {
                ++level;
            }

            // Close namespaces that are no longer needed
            for (size_t i = prev_ns.size(); i > level; --i) {
                ss << "} // namespace " << prev_ns[i - 1] << "\n";
            }

            if (level > 0) {
                ss << "\n";
            }

            // Open new namespaces
            for (size_t i = level; i < current_ns.size(); ++i) {
                ss << "namespace " << current_ns[i] << " {\n";
            }

            // Update the tracked namespaces
            prev_ns = current_ns;

            // Print the line and entry's source code
            ss << entry->to_source() << "\n";
        }

        first_line = false;
    }

    // Close any remaining open namespaces
    for (auto it = prev_ns.rbegin(); it != prev_ns.rend(); ++it) {
        ss << "} // namespace " << *it << "\n";
    }

    return ss.str();
}

std::ostream &operator<<(std::ostream &os, const SourceFile &sf)
{
    os << sf.to_source() << "\n";
    return os;
}

} // namespace dwarf2cpp
