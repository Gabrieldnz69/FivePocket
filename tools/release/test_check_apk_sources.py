#!/usr/bin/env python3
"""Tests for check_apk_sources.py with a minimal synthetic dex (only the header fields, string/type ids and class_defs it reads)."""
import io, struct, sys, tarfile, tempfile, unittest, zipfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import check_apk_sources as cas

def dex(classes, referenced=()):
    names = sorted(set(classes) | set(referenced))
    strings = b''; offs = []
    base = 0x70 + 4 * len(names) + 4 * len(names) + 32 * len(classes)
    for n in names:
        offs.append(base + len(strings)); b = n.encode(); strings += bytes([len(b)]) + b + b'\0'
    sid = b''.join(struct.pack('<I', o) for o in offs)
    tid = b''.join(struct.pack('<I', i) for i in range(len(names)))
    cdefs = b''.join(struct.pack('<8I', names.index(c), 1, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 0, 0) for c in classes)
    head = bytearray(0x70); head[:8] = b'dex\n035\0'
    struct.pack_into('<IIII', head, 0x38, len(names), 0x70, len(names), 0x70 + len(sid))
    struct.pack_into('<II', head, 0x60, len(classes), 0x70 + len(sid) + len(tid))
    return bytes(head) + sid + tid + cdefs + strings

class Check(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp())
        P = 'dev/recompbootstrap/smoke'
        with zipfile.ZipFile(self.t / 'a.apk', 'w') as z:
            z.writestr('classes.dex', dex([f'L{P}/Main;', f'L{P}/Main$1;', f'L{P}/Side-IA;', f'L{P}/R;', f'L{P}/Side;', 'Lother/X;'],
                                          referenced=[f'L{P}/OnlyReferenced;']))
        self.src = self.t / 'src'
        (self.src / 'rel/tree/app/src/main/java' / P).mkdir(parents=True)
        (self.src / 'rel/tree/app/src/main/res/values').mkdir(parents=True); (self.src / 'rel/tree/app/src/main/res/values/s.xml').write_text('<r/>')
        (self.src / 'rel/tree/app/src/main/java' / P / 'Main.java').write_text('package x;\npublic class Main {}\n')
        self.P = P

    def run_check(self, src):
        return cas.main(['--apk', f'{self.t}/a.apk=rel/tree', '--source', str(src)])

    def test_missing_class_fails_then_passes_when_its_source_is_added(self):
        self.assertEqual(self.run_check(self.src), 1)                     # Side has no source; R is generated (res/ present)
        (self.src / 'rel/tree/app/src/debug/java' / self.P).mkdir(parents=True)
        (self.src / 'rel/tree/app/src/debug/java' / self.P / 'Side.java').write_text('class Side {}\n')
        self.assertEqual(self.run_check(self.src), 0)

    def test_a_class_declared_in_another_file_counts_and_tarballs_work(self):
        (self.src / 'rel/tree/app/src/main/java' / self.P / 'Main.java').write_text('public class Main {}\nfinal class Side {}\n')
        tar = self.t / 's.tar.xz'
        with tarfile.open(tar, 'w:xz') as t: t.add(self.src / 'rel', arcname='rel')
        self.assertEqual(self.run_check(tar), 0)

    def test_the_wrong_tree_fails(self):
        # every class has its source in rel/tree; another tree of the same download must not count (each APK, its own tree)
        (self.src / 'rel/tree/app/src/main/java' / self.P / 'Main.java').write_text('public class Main {}\nclass Side {}\n')
        (self.src / 'rel/other/app/src/main/res/values').mkdir(parents=True); (self.src / 'rel/other/app/src/main/res/values/s.xml').write_text('<r/>')
        self.assertEqual(self.run_check(self.src), 0)
        self.assertEqual(cas.main(['--apk', f'{self.t}/a.apk=rel/other', '--source', str(self.src)]), 1)

    def test_r_needs_resources(self):
        (self.src / 'rel/tree/app/src/main/res/values/s.xml').unlink()
        (self.src / 'rel/tree/app/src/main/java' / self.P / 'Main.java').write_text('public class Main {}\nclass Side {}\n')
        self.assertEqual(self.run_check(self.src), 1)                     # R counts as missing without res/

if __name__ == '__main__':
    unittest.main()
