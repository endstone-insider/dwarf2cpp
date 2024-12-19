#pragma once

#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace posixpath {

static constexpr char sep = '/';

// Function to split a path into components
inline std::vector<std::string> split(const std::string &path)
{
    std::vector<std::string> components;
    std::istringstream stream(path);
    std::string segment;
    while (std::getline(stream, segment, sep)) {
        components.push_back(segment);
    }
    return components;
}

// Function to join components into a path using variadic parameters
inline std::string join(const std::vector<std::string> &paths)
{
    std::ostringstream joined;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) {
            joined << sep; // Add a separator between components
        }
        joined << paths[i];
    }
    return joined.str();
}

// Variadic join function
template <typename... Args>
std::string join(Args... paths)
{
    const std::vector<std::string> components = {paths...};
    return join(components);
}

// Function to find the common path
inline std::string commonpath(const std::vector<std::string> &paths)
{
    if (paths.empty()) {
        return "";
    }

    // Split all paths into components
    std::vector<std::vector<std::string>> split_paths;
    split_paths.reserve(paths.size());
    for (const auto &path : paths) {
        split_paths.push_back(split(path));
    }

    // Find the common prefix of all split paths
    std::vector<std::string> common;
    for (size_t i = 0; i < split_paths[0].size(); ++i) {
        std::string current = split_paths[0][i];
        for (size_t j = 1; j < split_paths.size(); ++j) {
            if (i >= split_paths[j].size() || split_paths[j][i] != current) {
                return join(common);
            }
        }
        common.push_back(current);
    }

    return join(common);
}

// Normalize path by removing redundant ".." and "." components
inline std::string normpath(const std::string &path)
{
    std::vector<std::string> components = split(path);
    std::vector<std::string> normalized;

    for (const auto &component : components) {
        if (component == "..") {
            if (!normalized.empty() && normalized.back() != "..") {
                normalized.pop_back(); // Go up one directory
            }
            else {
                normalized.push_back(component); // Keep ".." if no parent to go up to
            }
        }
        else if (component != ".") {
            normalized.push_back(component);
        }
    }

    return join(normalized);
}

// Compute the relative path
inline std::string relpath(const std::string &path, const std::string &start = "/")
{
    // Normalize the paths
    std::string norm_path = normpath(path);
    std::string norm_start = normpath(start);

    // Split the normalized paths into components
    std::vector<std::string> path_components = split(norm_path);
    std::vector<std::string> start_components = split(norm_start);

    // Find the common prefix
    size_t common_length = 0;
    for (size_t i = 0; i < std::min(path_components.size(), start_components.size()); ++i) {
        if (path_components[i] == start_components[i]) {
            ++common_length;
        }
        else {
            break;
        }
    }

    // Compute the relative path
    std::vector<std::string> relative_components;

    // Add ".." for each remaining component in the start path
    for (size_t i = common_length; i < start_components.size(); ++i) {
        relative_components.emplace_back("..");
    }

    // Add the remaining components of the target path
    for (size_t i = common_length; i < path_components.size(); ++i) {
        relative_components.push_back(path_components[i]);
    }

    // Join the components into the final relative path
    return relative_components.empty() ? "." : join(relative_components);
}

} // namespace posixpath
