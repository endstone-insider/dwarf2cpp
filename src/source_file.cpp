#include "source_file.h"

#include <sstream>
#include <utility>

namespace dwarf2cpp {

void SourceFile::add(std::size_t line, Entry *entry)
{
    lines_[line].push_back(entry);
}

std::string SourceFile::to_source() const
{
    std::stringstream ss;
    std::vector<std::string> prev_ns;
    for (const auto &[line, entries] : lines_) {
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

            // Open new namespaces
            for (size_t i = level; i < current_ns.size(); ++i) {
                ss << "namespace " << current_ns[i] << " {\n";
            }

            // Update the tracked namespaces
            prev_ns = current_ns;

            // Print the line and entry's source code
            ss << "// Line " << line << "\n";
            ss << entry->to_source() << "\n";
        }
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
