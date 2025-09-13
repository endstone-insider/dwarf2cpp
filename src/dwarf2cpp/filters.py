import re

from .models import Namespace


def do_ns_chain(ns: Namespace | None) -> list[Namespace]:
    chain = []
    while ns is not None:
        chain.append(ns)
        ns = ns.parent
    return list(reversed(chain))


def do_ns_actions(prev: list[Namespace], curr: list[Namespace]):
    i = 0
    while i < len(prev) and i < len(curr):
        if (prev[i].name, prev[i].is_inline) != (curr[i].name, curr[i].is_inline):
            break
        i += 1

    actions = []

    for n in reversed(prev[i:]):
        actions.append({"kind": "close", "namespace": n})

    for n in curr[i:]:
        actions.append({"kind": "open", "namespace": n})

    return actions


def do_type_cleanup(type_name: str) -> str:
    # Replace every occurrence of 'std::__1::' with 'std::'
    type_name = type_name.replace("std::__1::", "std::")
    type_name = type_name.replace("std::__ndk1::", "std::")
    type_name = type_name.replace(
        "std::basic_string<char, std::char_traits<char>, std::allocator<char> >",
        "std::string",
    )
    type_name = type_name.replace(
        "std::basic_string_view<char, std::char_traits<char> >",
        "std::string_view",
    )
    type_name = type_name.replace(
        "std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<long long, std::ratio<1L, 1000000000L> > >",
        "std::chrono::steady_clock::time_point",
    )
    pattern_repl = [
        # std::unique_ptr
        (
            r"std::unique_ptr<(.+?),\s*std::default_delete<\1\s*>\s*>",
            r"std::unique_ptr<\1>",
        ),
        # std::vector
        (r"std::vector<(.+?),\s*std::allocator<\1\s*>\s*>", r"std::vector<\1>"),
        # std::list
        (r"std::list<(.+?),\s*std::allocator<\1\s*>\s*>", r"std::list<\1>"),
        # std::deque
        (r"std::deque<(.+?),\s*std::allocator<\1\s*>\s*>", r"std::deque<\1>"),
        # std::queue
        (r"std::queue<(.+?),\s*std::deque<\1\s*>\s*>", r"std::queue<\1>"),
        # std::unordered_map
        (
            r"std::unordered_map<(.+?),\s*(.+?),\s*"
            r"std::hash<\1\s*>,\s*"
            r"std::equal_to<\1\s*>,\s*"
            r"std::allocator<std::pair<const \1,\s*\2\s*>\s*>\s*>",
            r"std::unordered_map<\1, \2>",
        ),
        # std::unordered_set
        (
            r"std::unordered_set<(.+?),\s*std::hash<\1\s*>,\s*"
            r"std::equal_to<\1\s*>,\s*std::allocator<\1\s*>\s*>",
            r"std::unordered_set<\1>",
        ),
        # std::map
        (
            r"std::map<(.+?),\s*(.+?),\s*std::less<\1\s*>,\s*"
            r"std::allocator<std::pair<const \1,\s*\2\s*>\s*>\s*>",
            r"std::map<\1, \2>",
        ),
        # std::set
        (
            r"std::set<(.+?),\s*std::less<\1\s*>,\s*std::allocator<\1\s*>\s*>",
            r"std::set<\1>",
        ),
        # gsl::span
        (r"gsl::span<(.+),\s*\d+UL>", r"gsl::span<\1>"),
        # glm::vec
        (r"glm::vec<(\d),\s*float,\s*\(glm::qualifier\)0>", r"glm::vec\1"),
        (r"glm::vec<(\d),\s*int,\s*\(glm::qualifier\)0>", r"glm::ivec\1"),
        # glm::mat
        (r"glm::mat<(\d),\s*(\d),\s*float,\s*\(glm::qualifier\)0>", r"glm::mat\1x\2"),
        # Bedrock::Result
        (r"Bedrock::Result<(.+?),\s*std::error_code>", r"Bedrock::Result<\1>"),
    ]

    while True:
        output = type_name
        for pattern, repl in pattern_repl:
            output = re.sub(pattern, repl, output)

        if output == type_name:
            break

        type_name = output

    return type_name
