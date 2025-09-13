import re


def cleanup(content: str) -> str:
    # Replace every occurrence of 'std::__1::' with 'std::'
    content = content.replace("std::__1::", "std::")
    content = content.replace("std::__ndk1::", "std::")
    content = content.replace(
        "std::basic_string<char, std::char_traits<char>, std::allocator<char> >",
        "std::string",
    )
    content = content.replace(
        "std::basic_string_view<char, std::char_traits<char> >",
        "std::string_view",
    )
    content = content.replace(
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
            r"std::allocator<std::pair<(?:const\s*\1|\1\s*const),\s*\2\s*>\s*>\s*>",
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
            r"std::allocator<std::pair<(?:const\s*\1|\1\s*const),\s*\2\s*>\s*>\s*>",
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
        output = content
        for pattern, repl in pattern_repl:
            output = re.sub(pattern, repl, output)

        if output == content:
            break

        content = output

    return content
