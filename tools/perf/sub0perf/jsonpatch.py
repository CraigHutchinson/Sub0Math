"""Minimal-diff edits of a JSON text: change a value, add or remove an object member, append to an array.

A committed spec (`perf/metrics.json`) is read by people and reviewed in diffs, so accepting one metric must not reformat the
file. `Doc` keeps the text and edits only the spans that change: the indentation, key order, line breaks, number spelling and
everything else stay byte for byte. New members and items copy the layout of their neighbours.

    doc = Doc(text)
    doc.replace(["metrics", "size", "expected"], 32)
    doc.remove(["metrics", "steps", "xfail"])
    doc.add(["metrics", "size"], "unit", "B")           # an object member
    doc.append(["accepted"], {"date": "2026-01-01"})    # an array item
    new_text = doc.text
"""

import json

_DECODER = json.JSONDecoder()
_SPACE = " \t\r\n"


class PatchError(ValueError):
    pass


class Node:
    """A parsed JSON value: its span in the text, and its members (object: [(key, key_start, Node)]) or items (array: [Node])."""

    def __init__(self, kind, start, end, members=None, items=None):
        self.kind, self.start, self.end, self.members, self.items = kind, start, end, members or [], items or []


def _skip(text, i):
    while i < len(text) and text[i] in _SPACE:
        i += 1
    return i


def _parse(text, i):
    i = _skip(text, i)
    if i >= len(text):
        raise PatchError("unexpected end of JSON")
    if text[i] == "{":
        members, j = [], _skip(text, i + 1)
        if text[j] == "}":
            return Node("object", i, j + 1)
        while True:
            j = _skip(text, j)
            key, kend = _DECODER.raw_decode(text, j)
            colon = _skip(text, kend)
            if text[colon] != ":":
                raise PatchError(f"expected ':' at {colon}")
            value = _parse(text, colon + 1)
            members.append((key, j, value))
            j = _skip(text, value.end)
            if text[j] == "}":
                return Node("object", i, j + 1, members=members)
            if text[j] != ",":
                raise PatchError(f"expected ',' or '}}' at {j}")
            j += 1
    if text[i] == "[":
        items, j = [], _skip(text, i + 1)
        if text[j] == "]":
            return Node("array", i, j + 1)
        while True:
            item = _parse(text, j)
            items.append(item)
            j = _skip(text, item.end)
            if text[j] == "]":
                return Node("array", i, j + 1, items=items)
            if text[j] != ",":
                raise PatchError(f"expected ',' or ']' at {j}")
            j += 1
    _, end = _DECODER.raw_decode(text, i)
    return Node("value", i, end)


def parse(text):
    """The root Node of a JSON text; PatchError (a ValueError) when it is not one document."""
    try:
        root = _parse(text, 0)
    except IndexError:
        raise PatchError("unexpected end of JSON") from None
    except ValueError as error:
        raise PatchError(str(error)) from None
    if _skip(text, root.end) != len(text):
        raise PatchError("extra data after the JSON document")
    return root


class Doc:
    """A JSON text and the edits made to it (each edit re-parses, so paths stay valid)."""

    def __init__(self, text):
        self.text = text
        self.root = parse(text)
        self.nl = "\r\n" if "\r\n" in text else "\n"

    # -- navigation

    def node(self, path):
        node = self.root
        for step in path:
            if isinstance(step, int):
                node = node.items[step]
                continue
            found = [v for k, _, v in node.members if k == step] if node.kind == "object" else []
            if not found:
                raise PatchError(f"no member {step!r} at {'/'.join(map(str, path))}")
            node = found[-1]
        return node

    def has(self, path):
        try:
            self.node(path)
            return True
        except (PatchError, IndexError):
            return False

    def _splice(self, start, end, replacement):
        self.text = self.text[:start] + replacement + self.text[end:]
        self.root = parse(self.text)

    # -- layout

    def _multiline(self):
        return "\n" in self.text.strip()

    def _line_indent(self, pos):
        """The leading whitespace of the line containing `pos`."""
        line_start = self.text.rfind("\n", 0, pos) + 1
        end = line_start
        while end < len(self.text) and self.text[end] in " \t":
            end += 1
        return self.text[line_start:end]

    def _unit(self):
        """One indentation step of this document: the first indented line's indent, else two spaces."""
        for line in self.text.split("\n")[1:]:
            stripped = line.lstrip(" \t")
            if stripped and stripped != line:
                return line[:len(line) - len(stripped)]
        return "  "

    def _sibling_inline(self, node):
        """True when the container's last child is written on one line (so a new container child is too)."""
        last = node.members[-1][2] if node.members else node.items[-1] if node.items else None
        return last is not None and "\n" not in self.text[last.start:last.end]

    def _render(self, value, indent, inline=False):
        """`value` as JSON for a member or item that starts a line indented by `indent` (None, or `inline`: one line)."""
        if indent is None or inline or not isinstance(value, (dict, list)):
            return json.dumps(value, ensure_ascii=False)
        unit = self._unit()
        text = json.dumps(value, indent="\t" if unit[0] == "\t" else len(unit), ensure_ascii=False)
        return text.replace("\n", "\n" + indent)

    def _colon(self, node):
        """The text between a key and its value in this object (`: ` by default, as a neighbour has it)."""
        if node.members:
            key, kstart, value = node.members[-1]
            return self.text[kstart + len(json.dumps(key, ensure_ascii=False)):value.start]
        return ": "

    def _insert(self, node, pieces):
        """Inserts `pieces` (rendered member or item texts, given indent) as the last child of a container node."""
        children = node.members if node.kind == "object" else node.items
        multiline = self._multiline()
        if children:
            first_start = children[-1][1] if node.kind == "object" else children[-1].start
            last_end = (children[-1][2] if node.kind == "object" else children[-1]).end
            before = self.text[:first_start]
            ws = before[len(before.rstrip(_SPACE)):]  # the whitespace before the last child
            lead = ws if "\n" in ws or not multiline else self.nl + self._line_indent(node.start) + self._unit()
            indent = lead[lead.rfind("\n") + 1:] if "\n" in lead else None
            lead = lead.replace("\r\n", "\n").replace("\n", self.nl)
            text = "".join("," + lead + piece(indent) for piece in pieces)
            self._splice(last_end, last_end, text)
            return
        base = self._line_indent(node.start)
        if multiline:
            indent = base + self._unit()
            body = self.nl + indent + (("," + self.nl + indent).join(piece(indent) for piece in pieces)) + self.nl + base
        else:
            body = ", ".join(piece(None) for piece in pieces)
        self._splice(node.start + 1, node.end - 1, body)

    # -- edits

    def replace(self, path, value):
        """Changes the value at `path` (its text only)."""
        node = self.node(path)
        self._splice(node.start, node.end, json.dumps(value, ensure_ascii=False))

    def add(self, path, key, value):
        """Adds member `key` to the object at `path`, laid out like its neighbours (last)."""
        obj = self.node(path)
        if obj.kind != "object":
            raise PatchError("not an object")
        colon = self._colon(obj)
        quoted = json.dumps(key, ensure_ascii=False)
        inline = self._sibling_inline(obj)
        self._insert(obj, [lambda indent: quoted + colon + self._render(value, indent, inline)])

    def append(self, path, value):
        """Appends `value` to the array at `path`."""
        arr = self.node(path)
        if arr.kind != "array":
            raise PatchError("not an array")
        inline = self._sibling_inline(arr)
        self._insert(arr, [lambda indent: self._render(value, indent, inline)])

    def remove(self, path):
        """Removes the object member at `path` with its separator."""
        *parent_path, key = path
        parent = self.node(parent_path)
        members = parent.members
        index = max(i for i, (k, _, _) in enumerate(members) if k == key) if any(k == key for k, _, _ in members) else None
        if index is None:
            raise PatchError(f"no member {key!r}")
        if len(members) == 1:
            self._splice(parent.start + 1, parent.end - 1, "")
        elif index < len(members) - 1:
            self._splice(members[index][1], members[index + 1][1], "")
        else:
            self._splice(members[index - 1][2].end, members[index][2].end, "")
