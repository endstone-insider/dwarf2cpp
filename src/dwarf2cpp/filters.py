import functools

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


@functools.cache
def do_insert_name(tp: str | tuple[str, str], name: str):
    if isinstance(tp, tuple):
        if not name:
            return "".join(tp)

        if tp[0].endswith("*") and not tp[1].startswith("["):
            return f"{tp[0]}{name}{tp[1]}"

        return f"{tp[0]} {name}{tp[1]}"

    if not name:
        return tp

    return f"{tp} {name}"
