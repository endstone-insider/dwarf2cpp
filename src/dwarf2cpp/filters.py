import functools
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


@functools.cache
def do_insert_name(tp: str, name: str):
    """Insert `name` into the C++ declarator described by `tp`."""
    t = tp.strip()

    if not name:
        return t

    # Arrays:  BASE [N][M]...
    m = re.match(r"^(?P<base>.+?)(?P<arrays>(?:\s*\[[^\]]*\]\s*)+)$", t)
    if m:
        base = m.group("base").strip()
        arrays = m.group("arrays").strip()
        return f"{base} {name}{arrays}"

    # Plain C-style function pointer:  RET (* )(ARGS...)
    m = re.match(r"^(?P<ret>.+?)\s*\(\s*\*\s*\)\s*\((?P<args>.*)\)$", t)
    if m:
        ret = m.group("ret").strip()
        args = m.group("args").strip()
        return f"{ret} (*{name})({args})"

    # # Member function pointer:  RET (Class::*)(ARGS...) [cv/ref qualifiers]
    # m = re.match(r'^(?P<ret>.+?)\s*\(\s*(?P<cls>.+::\*)\s*\)\s*(?P<rest>.*)$', t)
    # if m:
    #     ret = m.group('ret').strip()
    #     cls_ptr = m.group('cls').strip()  # e.g. Bedrock::JSONObject::Node::*
    #     rest = m.group('rest').strip()  # e.g. (bool) const
    #     return f"{ret} ({cls_ptr}{name}){(' ' + rest) if rest and not rest.startswith('(') else rest}"

    # Fallback: just a normal "type name"
    return f"{t} {name}"
