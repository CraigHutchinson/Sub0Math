"""Tests for the minimal-diff JSON editor behind `accept --metric`: only the changed spans differ."""

import json
import unittest

from ..jsonpatch import Doc, PatchError

TEXT = """{
  "schema": "sub0perf-metrics-spec-1",
  "metrics": {
    "a b": {"kind": "exact", "expected": 24, "unit": "B", "xfail": "why"},
    "steps": {
        "kind": "max",
        "limit": 4
    },
    "z": {"kind": "exact", "expected": 1}
  },
  "accepted": []
}
"""


class JsonPatchTest(unittest.TestCase):
    def test_replace_changes_only_the_value(self):
        doc = Doc(TEXT)
        doc.replace(["metrics", "a b", "expected"], 32)
        self.assertEqual(doc.text, TEXT.replace('"expected": 24', '"expected": 32'))

    def test_remove_a_middle_and_a_last_member(self):
        doc = Doc(TEXT)
        doc.remove(["metrics", "a b", "xfail"])
        self.assertEqual(doc.text, TEXT.replace(', "xfail": "why"', ""))
        doc.remove(["metrics", "steps", "limit"])  # the last member of a multi-line object
        self.assertEqual(json.loads(doc.text)["metrics"]["steps"], {"kind": "max"})
        doc = Doc(TEXT)
        doc.remove(["metrics", "steps", "kind"])  # the first of two
        self.assertEqual(json.loads(doc.text)["metrics"]["steps"], {"limit": 4})
        self.assertIn('        "limit": 4\n', doc.text)

    def test_add_follows_the_neighbours_layout(self):
        doc = Doc(TEXT)
        doc.add(["metrics"], "new", {"kind": "exact", "expected": 3})
        self.assertIn('    "z": {"kind": "exact", "expected": 1},\n    "new": {"kind": "exact", "expected": 3}\n  }', doc.text)
        doc.add(["metrics", "steps"], "unit", "n")
        self.assertIn('        "limit": 4,\n        "unit": "n"\n    }', doc.text)

    def test_append_to_an_empty_and_a_filled_array(self):
        doc = Doc(TEXT)
        doc.append(["accepted"], {"date": "d", "metrics": {"a": 1}})
        doc.append(["accepted"], {"date": "e"})
        data = json.loads(doc.text)
        self.assertEqual([x["date"] for x in data["accepted"]], ["d", "e"])
        self.assertTrue(doc.text.startswith(TEXT[:TEXT.index('"accepted"')]))
        self.assertTrue(doc.text.endswith("  ]\n}\n"))

    def test_compact_and_crlf_and_tab_documents(self):
        doc = Doc('{"metrics":{"a":{"kind":"exact","expected":1}},"accepted":[]}')
        doc.add(["metrics"], "b", {"kind": "max", "limit": 2})
        doc.append(["accepted"], {"r": 1})
        self.assertEqual(json.loads(doc.text), {"metrics": {"a": {"kind": "exact", "expected": 1}, "b": {"kind": "max", "limit": 2}},
                                                "accepted": [{"r": 1}]})
        self.assertNotIn("\n", doc.text)
        crlf = Doc(TEXT.replace("\n", "\r\n"))
        crlf.add(["metrics"], "n", {"kind": "exact", "expected": 3})
        self.assertNotIn("\n", crlf.text.replace("\r\n", ""))
        tabbed = Doc('{\n\t"metrics": {\n\t\t"a": 1\n\t},\n\t"accepted": []\n}\n')
        tabbed.append(["accepted"], {"x": 1})
        self.assertIn('\t"accepted": [\n\t\t{\n\t\t\t"x": 1\n\t\t}\n\t]', tabbed.text)

    def test_unknown_paths_and_bad_json_raise(self):
        with self.assertRaises(PatchError):
            Doc(TEXT).replace(["metrics", "missing", "expected"], 1)
        with self.assertRaises(PatchError):
            Doc(TEXT).remove(["metrics", "a b", "nope"])
        with self.assertRaises(ValueError):
            Doc("{")


if __name__ == "__main__":
    unittest.main()
