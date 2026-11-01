#!/usr/bin/env python3
"""Exercise #660 metadata against corrupt images and real x32 toolchain output.

Run from any directory with a host C compiler, an x32-capable gcc/ld, and
kconfiglib.  All generated files stay outside the source tree.
"""

import ctypes as C
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CC = shlex.split(os.environ.get("CC_HOST", "cc"))
X32_CC = shlex.split(os.environ.get("X32_CC", "gcc"))


def run(args):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        print(result.stdout, end="", file=sys.stderr)
        print(result.stderr, end="", file=sys.stderr)
        result.check_returncode()


class Symbol(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ("name", "value", "size")]
    _fields_ += [("info", C.c_ubyte), ("other", C.c_ubyte), ("shndx", C.c_uint16)]


class Object(C.Structure):
    _fields_ = [("image", C.c_void_p)]
    _fields_ += [(n, C.c_uint32) for n in (
        "image_size", "link_base", "bias", "symtab", "strtab", "strsz",
        "hash", "nbucket", "nchain")]
    _fields_ += [("needed", C.c_uint32 * 32), ("needed_count", C.c_uint32),
                ("soname", C.c_uint32), ("has_soname", C.c_int)]
    _fields_ += [(n, C.c_uint32) for n in (
        "dynamic", "dynamic_size", "rela", "relasz", "relaent",
        "jmprel", "pltrelsz", "pltrel", "rel", "relsz", "relent",
        "flags", "flags1", "features", "reloc_seen")]


class WriteRange(C.Structure):
    _fields_ = [("address", C.c_uint32), ("size", C.c_uint32)]


class Write(C.Structure):
    _fields_ = [("offset", C.c_uint32), ("width", C.c_uint32), ("value", C.c_uint64)]


class Backend(C.Structure):
    _fields_ = [("kind", C.c_void_p), ("value", C.c_void_p)]


class ImageAbi(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ("machine", "endian", "page_size", "max_span")]


class Load(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ("offset", "address", "filesz", "memsz", "flags", "align")]


class Image(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ("base", "size", "entry", "dynamic", "dynamic_size",
        "interp_offset", "interp_size", "phdr", "phnum", "align", "relro", "relro_size", "nload", "type")]
    _fields_ += [("loads", Load * 32)]


class StartInfo(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ("phdr", "phnum", "entry", "base", "pagesz", "basepage", "bootdrive", "load_bias", "capture_launch")]


class Mapping(C.Structure):
    _fields_ = [("object", Object), ("identity", C.c_uint64), ("cookie", C.c_void_p)]


class GraphNode(C.Structure):
    _fields_ = [("mapping", Mapping), ("name", C.c_char_p), ("edges", C.c_uint16 * 32)]


class Graph(C.Structure):
    _fields_ = [("nodes", GraphNode * 128), ("scope", C.POINTER(Object) * 128),
                ("order", C.c_uint16 * 128), ("count", C.c_uint32),
                ("order_count", C.c_uint32)]


Acquire = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.POINTER(Mapping))
Release = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(Mapping))


class GraphOps(C.Structure):
    _fields_ = [("acquire", Acquire), ("release", Release)]


def fixture():
    data = bytearray(1024)
    # One bucket, two symbols; symbol 1 is the definition of "value".
    struct.pack_into("=5I", data, 128, 1, 2, 1, 0, 0)
    names = b"\0value\0libtest.so\0libleaf.so\0"
    data[256:256 + len(names)] = names
    struct.pack_into("=IIIBBH", data, 192 + 16, 1, 512, 4, 0x11, 0, 1)
    tags = [(4, 128), (5, 256), (6, 192), (10, len(names)), (11, 16),
            (14, 7), (1, 18), (0, 0)]
    for i, pair in enumerate(tags):
        struct.pack_into("=II", data, i * 8, *pair)
    return data


def relocation_fixture(entries=(), plt=(), extra=()):
    data = fixture() + bytearray(2048)
    data[2048:2304] = b"\xa5" * 256
    tags = [struct.unpack_from("=II", data, i * 8) for i in range(7)]
    if entries:
        tags += [(7, 1536), (8, len(entries) * 12), (9, 12)]
    if plt:
        tags += [(23, 1800), (2, len(plt) * 12), (20, 7)]
    for start, table in ((1536, entries), (1800, plt)):
        for i, (offset, kind, symbol, addend) in enumerate(table):
            struct.pack_into("=IIi", data, start + 12 * i, offset, (symbol << 8) | kind, addend)
    tags += list(extra) + [(0, 0)]
    for i, pair in enumerate(tags):
        struct.pack_into("=II", data, 1024 + i * 8, *pair)
    return data, 1024, len(tags) * 8


def image_fixture(base=0, executable=False):
    data = bytearray(0x2200)
    data[:7] = b"\x7fELF\x01\x01\x01"
    interp = b"/GEMSYS/LIB/X32/LD.SO\0"
    headers = [
        (1, 0, base, 0, 0x400, 0x400, 4, 4096),
        (1, 0x1000, base + 0x1000, 0, 0x100, 0x100, 5, 4096),
        (1, 0x2000, base + 0x3000, 0, 0x200, 0x1300, 6, 4096),
        (2, 0x2000, base + 0x3000, 0, 48, 48, 6, 4),
        (0x6474e551, 0, 0, 0, 0, 0, 6, 16),
        (0x6474e552, 0x2000, base + 0x3000, 0, 0x200, 0x1000, 4, 1)]
    if executable:
        headers.append((3, 0x300, base + 0x300, 0, len(interp), len(interp), 4, 1))
        data[0x300:0x300 + len(interp)] = interp
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, 2 if executable else 3, 62, 1,
                     base + 0x1000, 52, 0, 0, 52, 32, len(headers), 0, 0, 0)
    for i, ph in enumerate(headers):
        struct.pack_into("<8I", data, 52 + i * 32, *ph)
    struct.pack_into("<5I", data, 0x200, 1, 2, 1, 0, 0)
    struct.pack_into("<IIIBBH", data, 0x250, 1, base + 0x4100, 4, 0x11, 0, 1)
    data[0x280:0x287] = b"\0value\0"
    for i, pair in enumerate(((4, base + 0x200), (5, base + 0x280), (6, base + 0x240),
                              (10, 7), (11, 16), (0, 0))):
        struct.pack_into("<II", data, 0x2000 + i * 8, *pair)
    data[0x1000:0x1100] = b"\xcc" * 256
    data[0x2100:0x2200] = b"\x5a" * 256
    return data


class Metadata(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="ptos-elf-shared-")
        cls.path = Path(cls.tmp.name)
        hostlib = cls.path / "dynamic.so"
        run(CC + ["-std=c90", "-pedantic", "-Wall", "-Wextra", "-Werror",
                  "-O2", "-fPIC", "-shared", "-I" + str(ROOT / "lib/ldso"),
                  str(ROOT / "lib/ldso/dynamic.c"), str(ROOT / "lib/ldso/relocate.c"),
                  str(ROOT / "lib/ldso/arch/x32/reloc.c"), str(ROOT / "lib/ldso/graph.c"),
                  str(ROOT / "lib/ldso/image.c"), str(ROOT / "lib/ldso/image_plan.c"),
                  str(ROOT / "lib/ldso/startup.c"), str(ROOT / "lib/ldso/arch/x32/image_abi.c"),
                  "-o", str(hostlib)])
        cls.lib = C.CDLL(str(hostlib))
        cls.lib.dl_read_dynamic.argtypes = [C.POINTER(Object), C.c_void_p] + [C.c_uint32] * 5
        cls.lib.dl_read_dynamic.restype = C.c_int
        cls.lib.dl_string.argtypes = [C.POINTER(Object), C.c_uint32]
        cls.lib.dl_string.restype = C.c_char_p
        cls.lib.dl_find_symbol.argtypes = [C.POINTER(Object), C.c_char_p,
                                          C.POINTER(Symbol), C.POINTER(C.c_uint32)]
        cls.lib.dl_find_symbol.restype = C.c_int
        cls.lib.dl_resolve_symbol.argtypes = [C.POINTER(Object), C.c_uint32,
                                             C.POINTER(C.POINTER(Object)), C.c_uint32,
                                             C.POINTER(C.c_uint32)]
        cls.lib.dl_resolve_symbol.restype = C.c_int
        cls.lib.dl_relocate.argtypes = [C.POINTER(Object), C.c_void_p,
                                       C.POINTER(C.POINTER(Object)), C.c_uint32,
                                       C.POINTER(WriteRange), C.c_uint32,
                                       C.POINTER(Write), C.c_uint32,
                                       C.POINTER(C.c_uint32), C.POINTER(Backend)]
        cls.lib.dl_relocate.restype = C.c_int
        cls.lib.dl_graph_open.argtypes = [C.POINTER(Graph), C.POINTER(Mapping),
                                          C.POINTER(GraphOps), C.c_void_p]
        cls.lib.dl_graph_open.restype = C.c_int
        cls.lib.dl_graph_close.argtypes = [C.POINTER(Graph), C.POINTER(GraphOps), C.c_void_p]
        cls.lib.dl_image_read.argtypes = [C.POINTER(Image), C.c_void_p, C.c_uint32,
                                          C.c_uint32, C.POINTER(ImageAbi)]
        cls.lib.dl_image_read.restype = C.c_int
        cls.lib.dl_image_copy.argtypes = [C.POINTER(Image), C.c_void_p, C.c_uint32,
                                          C.c_void_p, C.c_uint32, C.c_uint32, C.POINTER(Object)]
        cls.lib.dl_image_copy.restype = C.c_int
        cls.lib.dl_image_range.argtypes = [C.POINTER(Image), C.c_uint32, C.c_uint32, C.c_uint32]
        cls.lib.dl_image_range.restype = C.c_int
        cls.image_abi = ImageAbi.in_dll(cls.lib, "dl_x32_image_abi")
        cls.lib.dl_start_stack.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32,
            C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32,
            C.POINTER(StartInfo), C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]
        cls.lib.dl_start_stack.restype = C.c_int
        cls.backend = Backend.in_dll(cls.lib, "dl_x32_reloc_backend")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_fat_reclaim_retry_after_mirrored_write_failure(self):
        old_cwd = Path.cwd()
        old_prefix = os.environ.get("CONFIG_")
        try:
            os.chdir(ROOT)
            os.environ["CONFIG_"] = ""
            for pins in (False, True):
                config = self.path / "reclaim.config"
                text = (ROOT / "configs/pc-x86_64_defconfig").read_text()
                if not pins:
                    text += "\nCONF_WITH_X86_64_MEMTEST=n\n"
                config.write_text(text)
                run([sys.executable, str(ROOT / "tools/genconfig.py"),
                     "--kconfig", "Kconfig", "--config", str(config),
                     "--header", str(self.path / "autoconf.h"),
                     "--makefile", str(self.path / "auto.conf")])
                binary = self.path / "fat-reclaim"
                run(CC + ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                          "-ffunction-sections", "-fdata-sections", "-fno-builtin",
                          "-I" + str(self.path), "-Iinclude", "-Iinclude/arch/x86_64",
                          "-Ibdos", "-Ibios", "-Ifs", "tests/fat_reclaim/retry.c",
                          "-Wl,--gc-sections", "-o", str(binary)])
                run([str(binary)])
                if not pins:
                    config.write_text(text + "\nCONF_WITH_FAT_FILE_LIFETIMES=n\n")
                    run([sys.executable, str(ROOT / "tools/genconfig.py"),
                         "--kconfig", "Kconfig", "--config", str(config),
                         "--header", str(self.path / "autoconf.h"),
                         "--makefile", str(self.path / "auto.conf")])
                binary = self.path / "mkdir-full"
                run(CC + ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                          "-ffunction-sections", "-fdata-sections", "-fno-builtin",
                          "-I" + str(self.path), "-Iinclude", "-Iinclude/arch/x86_64",
                          "-Ibdos", "-Ibios", "-Ifs", "tests/fat_reclaim/mkdir_full.c",
                          "-Wl,--gc-sections", "-o", str(binary)])
                run([str(binary)])
            binary = self.path / "backing-reclaim"
            run(CC + ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                      "-ffunction-sections", "-fdata-sections", "-fno-builtin",
                      "-I" + str(self.path), "-Iinclude", "-Iinclude/arch/x86_64",
                      "-Ibdos", "-Ibios", "-Ifs", "tests/fat_reclaim/backing.c",
                      "-Wl,--gc-sections", "-o", str(binary)])
            run([str(binary)])
        finally:
            os.chdir(old_cwd)
            if old_prefix is None:
                os.environ.pop("CONFIG_", None)
            else:
                os.environ["CONFIG_"] = old_prefix

    def test_bootstrap_relative_widths_and_atomic_rejection(self):
        binary = self.path / "bootstrap-host"
        run(CC + ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                  "-I" + str(ROOT / "lib/ldso"),
                  str(ROOT / "tests/ldso_runtime/bootstrap_host.c"),
                  str(ROOT / "lib/ldso/arch/x32/bootstrap.c"), "-o", str(binary)])
        run([str(binary)])

    def parse(self, data, base=0, bias=0x1000000, dynamic=0, size=64):
        obj = Object()
        obj.image_size = 123  # Failure must clear any stale output.
        memory = C.create_string_buffer(bytes(data))
        rc = self.lib.dl_read_dynamic(C.byref(obj), memory, len(data), base,
                                      bias, dynamic, size)
        if rc:
            self.assertEqual(bytes(obj), bytes(Object()))
        return rc, obj, memory

    def lookup(self, obj, name=b"value"):
        sym = Symbol()
        address = C.c_uint32(0xDEADBEEF)
        rc = self.lib.dl_find_symbol(C.byref(obj), name, C.byref(sym), C.byref(address))
        if rc:
            self.assertEqual(address.value, 0xDEADBEEF)
            self.assertEqual(bytes(sym), bytes(Symbol()))
        return rc, sym, address.value

    def resolve(self, owner, index, objects):
        scope = (C.POINTER(Object) * len(objects))(*(C.pointer(o) for o in objects))
        address = C.c_uint32(0xDEADBEEF)
        rc = self.lib.dl_resolve_symbol(C.byref(owner), index, scope, len(objects), C.byref(address))
        if rc:
            self.assertEqual(address.value, 0xDEADBEEF)
        return rc, address.value

    def relocate(self, obj, memory, objects=None, ranges=((2048, 256),), capacity=64):
        objects = [obj] if objects is None else objects
        scope = (C.POINTER(Object) * len(objects))(*(C.pointer(o) for o in objects))
        writable = (WriteRange * len(ranges))(*(WriteRange(*r) for r in ranges))
        work = (Write * max(1, capacity))()
        written = C.c_uint32(123)
        before = bytes(memory)
        rc = self.lib.dl_relocate(C.byref(obj), memory, scope, len(objects), writable,
                                  len(ranges), work, capacity, C.byref(written), C.byref(self.backend))
        if rc:
            self.assertEqual(written.value, 0)
            self.assertEqual(bytes(memory), before, "failed relocation mutated the object")
        return rc, list(work)[:written.value]

    def parse_relocations(self, entries=(), plt=(), extra=(), bias=0x1000000, mutate=None):
        data, dynamic, size = relocation_fixture(entries, plt, extra)
        if mutate:
            mutate(data)
        rc, obj, memory = self.parse(data, bias=bias, dynamic=dynamic, size=size)
        self.assertEqual(rc, 0)
        return obj, memory

    def test_zero_sized_symbols_at_image_boundary(self):
        for info, visibility in [(0x10, 0), (0x11, 0), (0x01, 0), (0x11, 1), (0x11, 2), (0x11, 3)]:
            for offset, size, expected in [(0, 0, 0), (0, 1, -1), (1, 0, -1)]:
                with self.subTest(info=info, visibility=visibility, offset=offset, size=size):
                    data = fixture()
                    struct.pack_into("=IIBB", data, 212, len(data) + offset, size, info, visibility)
                    rc, obj, memory = self.parse(data)
                    self.assertEqual(rc, 0)
                    rc, value = self.resolve(obj, 1, [obj])
                    if expected == 0:
                        self.assertEqual((rc, value), (0, obj.bias + len(data)))
                    else:
                        self.assertNotEqual(rc, 0)
            # Even an empty boundary extent must not wrap its relocated VA.
            data = fixture()
            struct.pack_into("=IIBB", data, 212, len(data), 0, info, visibility)
            rc, obj, memory = self.parse(data)
            self.assertEqual(rc, 0)
            obj.bias = 0xffffffff - len(data) + 1
            self.assertNotEqual(self.resolve(obj, 1, [obj])[0], 0)

    def test_scope_interposition_and_local_binding(self):
        rc, owner, keep_owner = self.parse(fixture())
        data = fixture()
        struct.pack_into("=I", data, 212, 600)
        rc, other, keep_other = self.parse(data, bias=0x2000000)
        self.assertEqual(rc, 0)
        self.assertEqual(self.resolve(owner, 1, [other, owner]), (0, 0x2000258))
        for info, visibility in [(0x01, 0), (0x11, 1), (0x11, 2), (0x11, 3)]:
            with self.subTest(info=info, visibility=visibility):
                data = fixture()
                struct.pack_into("=BB", data, 220, info, visibility)
                rc, local, keep_local = self.parse(data)
                self.assertEqual(rc, 0)
                self.assertEqual(self.resolve(local, 1, [other, local]), (0, 0x1000200))
        self.assertEqual(self.resolve(owner, 0, []), (0, 0))
        self.assertEqual(self.resolve(owner, 99, [owner])[0], -1)

    def test_unresolved_and_weak_symbols(self):
        for info, expected in [(0x11, -3), (0x21, 0), (0x16, -2), (0x1A, -2)]:
            with self.subTest(info=info):
                def mutate(data):
                    struct.pack_into("=BBH", data, 220, info, 0, 0)
                obj, memory = self.parse_relocations([(2048, 6, 1, 99)], mutate=mutate)
                rc, plan = self.relocate(obj, memory)
                self.assertEqual(rc, expected)
                if not rc:
                    self.assertEqual(struct.unpack_from("=I", memory, 2048)[0], 0)

    def test_x32_relocation_widths_and_formulas(self):
        entries = [(2048, 8, 0, 17), (2064, 10, 1, -4), (2080, 6, 1, 123),
                   (2097, 1, 1, -1), (2112, 2, 1, 0), (2128, 11, 1, -10),
                   (2144, 38, 0, 21)]
        obj, memory = self.parse_relocations(entries, [(2160, 7, 1, 99)])
        rc, plan = self.relocate(obj, memory)
        self.assertEqual((rc, len(plan)), (0, 8))
        expected = [(2048, 4, 0x1000011), (2064, 4, 0x10001FC), (2080, 4, 0x1000200),
                    (2097, 8, 0x10001FF), (2112, 4, (-1600) & 0xFFFFFFFF),
                    (2128, 4, 0x10001F6), (2144, 8, 0x1000015), (2160, 4, 0x1000200)]
        for offset, width, value in expected:
            self.assertEqual(int.from_bytes(bytes(memory)[offset:offset + width], sys.byteorder), value)
            self.assertEqual(bytes(memory)[offset + width:offset + width + 4], b"\xa5" * 4)

    def test_64_bit_relocations_can_exceed_pointer_width(self):
        def absolute(data):
            struct.pack_into("=I", data, 212, 0xFFFFFFF0)
            struct.pack_into("=H", data, 222, 0xFFF1)
        obj, memory = self.parse_relocations([(2048, 1, 1, 32)], mutate=absolute)
        self.assertEqual(self.relocate(obj, memory)[0], 0)
        self.assertEqual(struct.unpack_from("=Q", memory, 2048)[0], 0x100000010)
        obj, memory = self.parse_relocations([(2048, 38, 0, 0x7FFFFFFF)], bias=0x90000000)
        self.assertEqual(self.relocate(obj, memory)[0], 0)
        self.assertEqual(struct.unpack_from("=Q", memory, 2048)[0], 0x10FFFFFFF)

    def test_relocation_failure_is_atomic(self):
        for name, bad, expected in [
                ("unsupported", (2064, 37, 0, 0), -2),
                ("symbol_index", (2064, 6, 99, 0), -1),
                ("relative_symbol", (2064, 8, 1, 0), -1),
                ("overlap", (2052, 6, 1, 0), -1),
                ("outside", (0xFFFFFFF0, 6, 1, 0), -1)]:
            with self.subTest(name=name):
                obj, memory = self.parse_relocations([(2048, 38, 0, 3), bad])
                self.assertEqual(self.relocate(obj, memory)[0], expected)
        for target in (128, 208, 256, 1024, 1536):
            with self.subTest(metadata=target):
                obj, memory = self.parse_relocations([(target, 8, 0, 0)])
                self.assertEqual(self.relocate(obj, memory, ranges=((0, 3072),))[0], -1)

    def test_relocation_overflow_and_writable_bounds(self):
        obj, memory = self.parse_relocations([(2048, 1, 1, 0)])
        self.assertEqual(self.relocate(obj, memory, ranges=((2048, 4),))[0], -1)
        obj, memory = self.parse_relocations([(2048, 8, 0, 0)], bias=0xFFFFF900)
        self.assertEqual(self.relocate(obj, memory)[0], -1)
        for kind, value in [(10, 0xFFFFFFF0), (11, 0x7FFFFFFF)]:
            with self.subTest(kind=kind):
                def absolute(data):
                    struct.pack_into("=I", data, 212, value)
                    struct.pack_into("=H", data, 222, 0xFFF1)
                obj, memory = self.parse_relocations([(2048, kind, 1, 32)], mutate=absolute)
                self.assertEqual(self.relocate(obj, memory)[0], -5)
        obj, memory = self.parse_relocations([(2048, 8, 0, 0x7FFFFFFF)], bias=0x90000000)
        self.assertEqual(self.relocate(obj, memory)[0], -5)

    def test_relocation_capacity_and_noop(self):
        for capacity in (0, 1):
            obj, memory = self.parse_relocations([(2048, 8, 0, 1), (2064, 8, 0, 2)])
            self.assertEqual(self.relocate(obj, memory, capacity=capacity)[0], -4)
        obj, memory = self.parse_relocations([(0xFFFFFFFF, 0, 0xFFFFFF, 0)])
        self.assertEqual(self.relocate(obj, memory, capacity=0), (0, []))

    def test_unsupported_dynamic_features(self):
        for tag, value in [(22, 0), (30, 4), (30, 2), (0x6FFFFFFB, 8),
                           (17, 1536), (36, 1536), (0x6FFFFFF0, 1536), (29, 7)]:
            with self.subTest(tag=tag, value=value):
                obj, memory = self.parse_relocations([(2048, 8, 0, 1)], extra=[(tag, value)])
                self.assertEqual(self.relocate(obj, memory)[0], -2)

    def test_plt_subset_is_relocated_once(self):
        def subset(data):
            for offset in range(1024, 1200, 8):
                if struct.unpack_from("=I", data, offset)[0] == 23:
                    struct.pack_into("=I", data, offset + 4, 1536)
                    return
        obj, memory = self.parse_relocations([(2048, 7, 1, 9)], [(2048, 7, 1, 9)], mutate=subset)
        rc, plan = self.relocate(obj, memory)
        self.assertEqual((rc, len(plan)), (0, 1))
        self.assertEqual(struct.unpack_from("=I", memory, 2048)[0], 0x1000200)

    def test_malformed_relocation_tables(self):
        for tag, value, expected in [(7, 0xFFFFFFF0, -1), (8, 13, -1),
                                      (9, 8, -1), (20, 17, -2), (23, 1540, -1)]:
            with self.subTest(tag=tag, value=value):
                def mutate(data):
                    for offset in range(1024, 1200, 8):
                        if struct.unpack_from("=I", data, offset)[0] == tag:
                            struct.pack_into("=I", data, offset + 4, value)
                            return
                obj, memory = self.parse_relocations([(2048, 8, 0, 1)],
                                                     [(2064, 7, 1, 0)], mutate=mutate)
                self.assertEqual(self.relocate(obj, memory)[0], expected)

    def test_dependency_soname_and_symbol(self):
        rc, obj, memory = self.parse(fixture())
        self.assertEqual(rc, 0)
        self.assertEqual(obj.needed_count, 1)
        self.assertEqual(self.lib.dl_string(C.byref(obj), obj.needed[0]), b"libleaf.so")
        self.assertEqual(self.lib.dl_string(C.byref(obj), obj.soname), b"libtest.so")
        rc, sym, address = self.lookup(obj)
        self.assertEqual((rc, sym.size, address), (0, 4, 0x1000200))
        self.assertEqual(self.lookup(obj, b"absent")[0], 1)

    def test_invalid_dynamic_tables(self):
        cases = [("unterminated", 56, 4), ("duplicate", 48, 4),
                 ("bad_syment", 36, 24), ("bad_string_extent", 28, 0xFFFFFFFF),
                 ("bad_needed", 52, 0xFFFFFFFF), ("empty_needed", 52, 0),
                 ("bad_soname", 44, 0xFFFFFFFF), ("empty_soname", 44, 0),
                 ("bad_symtab", 20, 0xFFFFFFF0), ("bad_hash", 4, 0xFFFFFFFF),
                 ("empty_buckets", 128, 0), ("huge_buckets", 128, 0xFFFFFFFF),
                 ("empty_chain", 132, 0), ("huge_chain", 132, 0xFFFFFFFF)]
        for name, offset, value in cases:
            with self.subTest(name=name):
                data = fixture()
                struct.pack_into("=I", data, offset, value)
                self.assertEqual(self.parse(data)[0], -1)
        self.assertEqual(self.parse(fixture(), size=63)[0], -1)
        self.assertEqual(self.parse(fixture(), dynamic=1020)[0], -1)
        self.assertEqual(self.parse(fixture(), base=0xFFFFFFF0)[0], -1)

    def test_unterminated_strings(self):
        data = fixture()
        data[256] = ord("x")
        self.assertEqual(self.parse(data)[0], -1)
        data = fixture()
        data[256 + 28] = ord("x")
        self.assertEqual(self.parse(data)[0], -1)

    def test_gnu_hash_only_is_explicitly_unsupported(self):
        data = fixture()
        struct.pack_into("=I", data, 0, 0x6FFFFEF5)
        self.assertEqual(self.parse(data)[0], -2)

    def test_dependency_cap(self):
        data = fixture()
        # Move the enlarged dynamic table away from metadata.
        tags = [(4, 128), (5, 256), (6, 192), (10, 29), (11, 16)]
        tags += [(1, 18)] * 33 + [(0, 0)]
        for i, pair in enumerate(tags):
            struct.pack_into("=II", data, 600 + i * 8, *pair)
        self.assertEqual(self.parse(data, dynamic=600, size=len(tags) * 8)[0], -2)

    def test_visibility_binding_and_undefined(self):
        for info, visibility, shndx, expected in [
                (0x01, 0, 1, 1), (0x11, 1, 1, 1), (0x11, 2, 1, 1),
                (0x11, 0, 0, 1), (0x21, 0, 1, 0), (0x11, 3, 1, 0),
                (0x16, 0, 1, -2), (0x1A, 0, 1, -2), (0x11, 0, 0xFFF2, -2)]:
            with self.subTest(info=info, visibility=visibility, shndx=shndx):
                data = fixture()
                struct.pack_into("=BBH", data, 208 + 12, info, visibility, shndx)
                rc, obj, memory = self.parse(data)
                self.assertEqual(rc, 0)
                self.assertEqual(self.lookup(obj)[0], expected)

    def test_absolute_and_overflowing_addresses(self):
        data = fixture()
        struct.pack_into("=H", data, 208 + 14, 0xFFF1)
        rc, obj, memory = self.parse(data)
        self.assertEqual(rc, 0)
        self.assertEqual(self.lookup(obj)[2], 512)
        rc, obj, memory = self.parse(fixture(), bias=0xFFFFFF00)
        self.assertEqual(rc, 0)
        self.assertEqual(self.lookup(obj)[0], -1)

    def test_bad_symbol_and_hash_chain(self):
        for name, offset, value in [("bucket", 136, 2), ("symbol_name", 208, 99),
                                     ("symbol_extent", 212, 1023), ("cycle", 144, 1)]:
            with self.subTest(name=name):
                data = fixture()
                struct.pack_into("=I", data, offset, value)
                rc, obj, memory = self.parse(data)
                self.assertEqual(rc, 0)
                query = b"absent" if name == "cycle" else b"value"
                self.assertEqual(self.lookup(obj, query)[0], -1)

    def test_unaligned_image(self):
        data = fixture()
        # Actual image pointer is unaligned, not just an ELF virtual address.
        memory = C.create_string_buffer(b"x" + bytes(data))
        obj = Object()
        self.assertEqual(self.lib.dl_read_dynamic(C.byref(obj), C.addressof(memory) + 1,
                                                len(data), 0, 0, 0, 64), 0)
        self.assertEqual(self.lookup(obj)[2], 512)

    def test_nonzero_link_base(self):
        data = fixture()
        base = 0x400000
        for offset in (4, 12, 20, 212):
            value = struct.unpack_from("=I", data, offset)[0]
            struct.pack_into("=I", data, offset, value + base)
        rc, obj, memory = self.parse(data, base=base, dynamic=base)
        self.assertEqual(rc, 0)
        self.assertEqual(self.lookup(obj)[2], 0x1400200)

    def startup(self, args=b'', env=b'CHECK=660\0SECOND=value\0\0', tail=None,
                capacity=131072, filename=b'C:\\DYNTEST.TOS', load_bias=0, capture=False):
        tail = bytes([len(args)]) + args if tail is None else tail
        if capture:
            tail = tail.ljust(128, b'\0')
        memory = C.create_string_buffer(capacity)
        info = StartInfo(0x400034, 10, 0x401000, 0x800000, 4096, 0x201000, 2, load_bias, int(capture))
        rsp, used = C.c_uint32(123), C.c_uint32(456)
        rc = self.lib.dl_start_stack(memory, capacity, 0x80000000,
            C.create_string_buffer(filename), len(filename) + 1,
            C.create_string_buffer(tail), len(tail), C.create_string_buffer(env), len(env),
            C.byref(info), C.byref(rsp), C.byref(used))
        if rc:
            self.assertEqual((rsp.value, used.value), (0, 0))
            return rc, None
        self.assertEqual(rsp.value & 15, 0)
        self.assertEqual(rsp.value + used.value, 0x80000000)
        data = bytes(memory)
        base = 0x80000000 - capacity

        def word(address):
            self.assertGreaterEqual(address, rsp.value)
            self.assertLessEqual(address, 0x80000000 - 4)
            return struct.unpack_from('=I', data, address - base)[0]

        def string(address):
            self.assertGreaterEqual(address, rsp.value)
            self.assertLess(address, 0x80000000)
            return data[address - base:data.index(b'\0', address - base)]

        argc = word(rsp.value)
        argv = [string(word(rsp.value + 4 + i * 4)) for i in range(argc)]
        at = rsp.value + 4 + argc * 4
        self.assertEqual(word(at), 0)
        at += 4
        envp = []
        while word(at):
            envp.append(string(word(at)))
            at += 4
        at += 4
        aux = {}
        while word(at):
            self.assertNotIn(word(at), aux)
            aux[word(at)] = word(at + 4)
            at += 8
        self.assertEqual(word(at + 4), 0)
        self.assertEqual((aux[3], aux[4], aux[5], aux[6], aux[7], aux[9], aux[0x5000]),
                         (info.phdr, 32, info.phnum, 4096, info.base, info.entry, info.basepage))
        self.assertEqual(string(aux[31]), filename)
        self.assertEqual(aux[0x5001], 2)
        self.assertEqual(aux[0x5002], info.load_bias)
        if capture:
            launch = aux[0x5003] - base
            self.assertEqual(data[launch:launch+264], bytes(264))
            self.assertEqual(data[launch+264:launch+392], tail[:128])
            self.assertEqual(aux[0x5003] & 3, 0)
        else:
            self.assertNotIn(0x5003, aux)
        return rc, (argv, envp, aux)

    def test_standard_initial_stack_launch_capture(self):
        self.assertEqual(self.startup(b'hello', capture=True)[0], 0)
        self.assertEqual(self.startup(tail=b'\x7f',
            env=b'FOO=bar\0ARGV=\0alias\0PATH=from-arg\0\0', capture=True)[0], 0)
        self.assertEqual(self.startup(capture=True, capacity=256)[0], -4)

    def test_standard_initial_stack_load_bias(self):
        for bias in (0, 0x400000):
            with self.subTest(load_bias=bias):
                rc, result = self.startup(load_bias=bias)
                self.assertEqual(rc, 0)
                self.assertEqual(result[2][0x5002], bias)

    def test_standard_initial_stack_short_tail(self):
        rc, result = self.startup(b'"hello world" "" C:\\tmp')
        self.assertEqual(rc, 0)
        self.assertEqual(result[0], [b'C:\\DYNTEST.TOS', b'hello world', b'', b'C:\\tmp'])
        self.assertEqual(result[1], [b'CHECK=660', b'SECOND=value'])
        rc, result = self.startup(b" a' b 'c \"d\"e ")
        self.assertEqual(result[0][1:], [b'a b c', b'de'])
        self.assertEqual(self.startup(env=b'\0\0')[1][1], [])

    def test_standard_initial_stack_extended_argv(self):
        env = b'CHECK=660\0SECOND=value\0ARGV=\0custom-name\0hello world\0C:\\tmp\0\0'
        rc, result = self.startup(env=env, tail=b'\x7f\0')
        self.assertEqual(rc, 0)
        self.assertEqual(result[0], [b'custom-name', b'hello world', b'C:\\tmp'])
        self.assertEqual(result[1], [b'CHECK=660', b'SECOND=value'])
        rc, result = self.startup(b'normal', env=env)
        self.assertEqual(result[0], [b'C:\\DYNTEST.TOS', b'normal'])
        self.assertEqual(result[1], [b'CHECK=660', b'SECOND=value'])

    def test_standard_initial_stack_malformed_and_capacity(self):
        self.assertEqual(self.startup(b'"unterminated')[0], -1)
        self.assertEqual(self.startup(tail=b'\x7f\0')[0], -1)
        self.assertEqual(self.startup(tail=b'\x80')[0], -1)
        self.assertEqual(self.startup(tail=b'\x05abc')[0], -1)
        self.assertEqual(self.startup(env=b'A=B')[0], -1)
        self.assertEqual(self.startup(env=b'\0x')[0], -1)
        self.assertEqual(self.startup(env=b'ARGV=NULL:1\0app\0a\0\0', tail=b'\x7f')[0], -2)
        self.assertEqual(self.startup(capacity=64)[0], -1)
        self.assertEqual(self.startup(b'x' * 126, capacity=128)[0], -4)
        self.assertEqual(self.startup(filename=b'x' * 1024)[0], -1)

    def test_standard_initial_stack_large_environment(self):
        rc, result = self.startup(env=b'X\0' * 16383 + b'\0')
        self.assertEqual(rc, 0)
        self.assertEqual(len(result[1]), 16383)
        self.assertEqual(self.startup(env=b'X\0' * 16384 + b'\0')[0], -1)

    def image_read(self, data, role=1, abi=None):
        raw, plan = C.create_string_buffer(bytes(data)), Image()
        C.memset(C.byref(plan), 0xa5, C.sizeof(plan))
        rc = self.lib.dl_image_read(C.byref(plan), raw, len(data), role,
                                    C.byref(self.image_abi if abi is None else abi))
        if rc:
            self.assertEqual(bytes(plan), bytes(Image()))
        return rc, plan, raw

    def image_copy(self, plan, raw, bias=0x1000000, capacity=None):
        capacity = plan.size if capacity is None else capacity
        memory, obj = C.create_string_buffer(b"\xa5" * max(1, capacity)), Object()
        rc = self.lib.dl_image_copy(C.byref(plan), raw, len(raw) - 1,
                                    memory, capacity, bias, C.byref(obj))
        if rc:
            self.assertEqual(bytes(obj), bytes(Object()))
        return rc, obj, memory

    def test_image_population_bss_holes_and_nonzero_link_base(self):
        for base in (0, 0x200000):
            with self.subTest(base=base):
                rc, plan, raw = self.image_read(image_fixture(base))
                self.assertEqual(rc, 0)
                self.assertEqual((plan.base, plan.size, plan.nload), (base, 0x5000, 3))
                self.assertEqual((plan.relro, plan.relro_size), (base + 0x3000, 0x1000))
                self.assertEqual(plan.phdr, base + 52)
                rc, obj, memory = self.image_copy(plan, raw)
                self.assertEqual(rc, 0)
                self.assertEqual((obj.link_base, obj.bias), (base, 0x1000000))
                self.assertEqual(bytes(memory)[0x1000:0x1100], b"\xcc" * 256)
                self.assertEqual(bytes(memory)[0x2100:0x3000], bytes(0xf00))
                self.assertEqual(bytes(memory)[0x400:0x1000], bytes(0xc00))
                self.assertEqual(bytes(memory)[0x3100:0x3200], b"\x5a" * 256)
                self.assertEqual(bytes(memory)[0x3200:0x5000], bytes(0x1e00))
                self.assertEqual(self.lookup(obj)[2], base + 0x1004100)
                self.assertEqual(self.lib.dl_image_range(C.byref(plan), base + 0x4100, 4, 6), 1)
                self.assertEqual(self.lib.dl_image_range(C.byref(plan), base + 0x2000, 4, 4), 0)

    def test_image_header_and_truncation_rejection(self):
        original = image_fixture()
        for cut in (0, 6, 51, 52, 52 + 6 * 32 - 1, len(original) - 1):
            self.assertEqual(self.image_read(original[:cut])[0], -1)
        for off, fmt, value in ((4, "B", 2), (5, "B", 2), (6, "B", 0),
             (16, "H", 1), (18, "H", 3), (20, "I", 0), (36, "I", 1),
             (40, "H", 64), (42, "H", 56), (44, "H", 0), (44, "H", 129),
             (28, "I", 0xfffffff0)):
            with self.subTest(offset=off, value=value):
                data = bytearray(original)
                struct.pack_into("<" + fmt, data, off, value)
                self.assertEqual(self.image_read(data)[0], -1)

    def test_image_load_bounds_alignment_and_page_permissions(self):
        for ph, field, value in ((0, 16, 0x500), (0, 4, 0x3000),
                (1, 8, 0xfffff000), (1, 20, 0xfffff000), (1, 28, 24),
                (1, 24, 7), (1, 24, 1), (1, 24, 0x84)):
            with self.subTest(ph=ph, field=field, value=value):
                data = image_fixture()
                struct.pack_into("<I", data, 52 + ph * 32 + field, value)
                self.assertEqual(self.image_read(data)[0], -1)
        # Nonoverlapping bytes in one page still cannot disagree on rights.
        data = image_fixture()
        struct.pack_into("<8I", data, 84, 1, 0x800, 0x800, 0, 0x100, 0x100, 5, 1)
        self.assertEqual(self.image_read(data)[0], -1)
        # Same permissions may share a page; overlapping bytes may not.
        struct.pack_into("<I", data, 84 + 24, 4)
        self.assertEqual(self.image_read(data)[0], 0)
        struct.pack_into("<8I", data, 84, 1, 0x300, 0x300, 0, 0x100, 0x100, 4, 1)
        self.assertEqual(self.image_read(data)[0], -1)

    def test_image_dynamic_and_relro_admission(self):
        for ph, field, value in ((3, 0, 0), (3, 4, 0x2008), (3, 8, 0x3200),
                (3, 16, 47), (3, 20, 0), (3, 20, 56), (5, 8, 0x1000), (5, 20, 0x2000), (5, 4, 0x2008), (5, 16, 0x2000)):
            with self.subTest(ph=ph, field=field, value=value):
                data = image_fixture()
                struct.pack_into("<I", data, 52 + ph * 32 + field, value)
                self.assertEqual(self.image_read(data)[0], -1)
        # Point strtab into zero-filled BSS. The metadata reader can find a
        # terminator there, but image admission must demand file-backed bytes.
        data = image_fixture()
        struct.pack_into("<I", data, 0x2000 + 8 + 4, 0x4100)
        rc, plan, raw = self.image_read(data)
        self.assertEqual(rc, 0)
        self.assertEqual(self.image_copy(plan, raw)[0], -1)
        for ph_type in (7, 0x60000001, 0x60000002):
            data = image_fixture()
            struct.pack_into("<I", data, 52 + 4 * 32, ph_type)
            self.assertEqual(self.image_read(data)[0], -2)
        data = image_fixture()
        struct.pack_into("<I", data, 52 + 4 * 32 + 24, 7)
        self.assertEqual(self.image_read(data)[0], -2)

    def test_image_roles_interpreter_entry_and_program_headers(self):
        data = image_fixture(executable=True)
        self.assertEqual(self.image_read(data, role=3)[0], 0)
        self.assertEqual(self.image_read(data, role=1)[0], -1)
        self.assertEqual(self.image_read(data, role=2)[0], -1)
        self.assertEqual(self.image_read(image_fixture(), role=2)[0], 0)
        self.assertEqual(self.image_read(image_fixture(), role=3)[0], -1)
        for value in (0x1100, 0x3000, 0xfffffff0):
            bad = bytearray(data)
            struct.pack_into("<I", bad, 24, value)
            self.assertEqual(self.image_read(bad, role=3)[0], -1)
        for replacement in (b"relative/path\0", b"/has\0nul/data\0", b"/unterminated"):
            bad = bytearray(data)
            bad[0x300:0x300 + len(replacement)] = replacement
            struct.pack_into("<I", bad, 52 + 6 * 32 + 16, len(replacement))
            self.assertEqual(self.image_read(bad, role=3)[0], -1)
        # Duplicate PT_INTERP and invalid explicit PT_PHDR must fail.
        bad = bytearray(data)
        bad[52 + 4 * 32:52 + 5 * 32] = bad[52 + 6 * 32:52 + 7 * 32]
        self.assertEqual(self.image_read(bad, role=3)[0], -1)
        bad = bytearray(data)
        struct.pack_into("<8I", bad, 52 + 4 * 32, 6, 52, 0xdead, 0, 224, 224, 4, 4)
        self.assertEqual(self.image_read(bad, role=3)[0], -1)

    def test_image_duplicate_headers_and_load_limit(self):
        for header in (3, 5):  # PT_DYNAMIC / PT_GNU_RELRO
            data = image_fixture()
            data[52 + 4 * 32:52 + 5 * 32] = data[52 + header * 32:52 + (header + 1) * 32]
            self.assertEqual(self.image_read(data)[0], -1)
        data = image_fixture()
        struct.pack_into("<H", data, 44, 34)
        # 33 distinct PT_LOAD ranges exceed the bounded plan capacity.
        for i in range(33):
            struct.pack_into("<8I", data, 52 + i * 32, 1, 0, i * 4096, 0, 0x400, 0x400, 4, 4096)
        struct.pack_into("<8I", data, 52 + 33 * 32, 2, 0, 0, 0, 8, 8, 4, 4)
        self.assertEqual(self.image_read(data)[0], -4)

    def test_image_copy_limits_and_runtime_bounds(self):
        rc, plan, raw = self.image_read(image_fixture())
        self.assertEqual(rc, 0)
        for bias, capacity in ((1, plan.size), (0xfffff000, plan.size), (0, plan.size - 1)):
            rc, obj, memory = self.image_copy(plan, raw, bias=bias, capacity=capacity)
            self.assertEqual(rc, -1)
            self.assertEqual(bytes(memory)[:capacity], b"\xa5" * capacity)
        # Larger p_align is legal, but its load bias must preserve alignment.
        data = image_fixture()
        struct.pack_into("<I", data, 84 + 28, 0x2000)
        rc, large, large_raw = self.image_read(data)
        self.assertEqual(rc, 0)
        self.assertEqual(large.align, 0x2000)
        self.assertEqual(self.image_copy(large, large_raw, bias=0x1001000)[0], -1)
        self.assertEqual(self.image_copy(large, large_raw, bias=0x1000000)[0], 0)
        small = ImageAbi(62, 1, 4096, 4096)
        self.assertEqual(self.image_read(image_fixture(), abi=small)[0], -4)
        rc, plan, raw = self.image_read(image_fixture(0x400000, executable=True), role=3)
        self.assertEqual(rc, 0)
        self.assertEqual(self.image_copy(plan, raw)[0], -1)  # ET_EXEC cannot move.
        self.assertEqual(self.image_copy(plan, raw, bias=0)[0], 0)

    def graph_object(self, soname, needed=(), extra=()):
        data = fixture() + bytearray(3072)
        names = bytearray(b"\0value\0")
        tags = [(4, 128), (5, 256), (6, 192), (11, 16)]
        if soname is not None:
            tags.append((14, len(names)))
            names += soname.encode() + b"\0"
        for name in needed:
            tags.append((1, len(names)))
            names += name.encode() + b"\0"
        data[256:256 + len(names)] = names
        tags += [(10, len(names)), *extra, (0, 0)]
        for i, pair in enumerate(tags):
            struct.pack_into("=II", data, 1024 + i * 8, *pair)
        rc, obj, mem = self.parse(data, dynamic=1024, size=len(tags) * 8)
        self.assertEqual(rc, 0)
        return obj, mem

    def graph_run(self, definitions, root="app", fail=None, identities=None, expected=0):
        objects = {name: self.graph_object(*spec) for name, spec in definitions.items()}
        ids = {name: i + 1 for i, name in enumerate(definitions)}
        ids.update(identities or {})
        acquired, released = [], []

        @Acquire
        def acquire(ctx, name, out):
            name = name.decode()
            acquired.append(name)
            if name == fail or name not in objects:
                return 1  # DL_NOT_FOUND
            out[0] = Mapping(objects[name][0], ids[name], ids[name])
            return 0

        @Release
        def release(ctx, mapping):
            released.append(mapping.contents.identity)

        ops = GraphOps(acquire, release)
        graph = Graph()
        rc = self.lib.dl_graph_open(C.byref(graph),
                    C.byref(Mapping(objects[root][0], ids[root], ids[root])), C.byref(ops), None)
        self.assertEqual(rc, expected)
        scope = [graph.nodes[i].mapping.identity for i in range(graph.count)]
        order = [scope[graph.order[i]] for i in range(graph.order_count)]
        if rc:
            self.assertEqual((graph.count, graph.order_count), (0, 0))
        self.lib.dl_graph_close(C.byref(graph), C.byref(ops), None)
        # Idempotent close must never drop a reference twice or release root.
        before = released[:]
        self.lib.dl_graph_close(C.byref(graph), C.byref(ops), None)
        self.assertEqual(released, before)
        self.assertNotIn(ids[root], released)
        # Keep object buffers alive until callbacks and graph are finished.
        self.assertEqual(len(objects), len(definitions))
        return acquired, released, scope, order

    def test_dependency_diamond_breadth_first_scope(self):
        a, r, scope, order = self.graph_run({
            "app": (None, ("a.so", "b.so")), "a.so": ("a.so", ("c.so",)),
            "b.so": ("b.so", ("c.so",)), "c.so": ("c.so", ())})
        self.assertEqual(a, ["a.so", "b.so", "c.so"])
        self.assertEqual(scope, [1, 2, 3, 4])
        self.assertEqual(order, [4, 2, 3, 1])
        self.assertEqual(r, [4, 3, 2])

    def test_dependency_cycles_and_duplicate_needed(self):
        a, r, scope, order = self.graph_run({
            "app": ("app", ("a.so", "a.so")),
            "a.so": ("a.so", ("b.so",)), "b.so": ("b.so", ("a.so", "app"))})
        self.assertEqual(a, ["a.so", "b.so"])
        self.assertEqual(order, [3, 2, 1])
        self.assertEqual(r, [3, 2])

    def test_dependency_missing_unwinds_every_acquisition(self):
        a, r, scope, order = self.graph_run({
            "app": (None, ("a.so", "missing.so")), "a.so": ("a.so", ())}, expected=1)
        self.assertEqual(a, ["a.so", "missing.so"])
        self.assertEqual(r, [2])

    def test_dependency_failure_at_each_acquire(self):
        defs = {"app": (None, ("a.so", "b.so")), "a.so": ("a.so", ("c.so",)),
                "b.so": ("b.so", ()), "c.so": ("c.so", ())}
        for i, name in enumerate(("a.so", "b.so", "c.so")):
            with self.subTest(name=name):
                a, released, _, _ = self.graph_run(defs, fail=name, expected=1)
                self.assertEqual(len(a), i + 1)
                self.assertEqual(released, list(range(i + 1, 1, -1)))

    def test_dependency_names_soname_and_features(self):
        for bad in (".", "..", "../a.so", "dir/a.so", "dir\\a.so", "C:a.so", "a\n.so"):
            with self.subTest(name=bad):
                a, r, _, _ = self.graph_run({"app": (None, (bad,))}, expected=-1)
                self.assertEqual(a, [])
                self.assertEqual(r, [])
        for spec, error in ((("other.so", ()), -1),
                            (("a.so", (), ((29, 1),)), -2)):
            a, r, _, _ = self.graph_run({"app": (None, ("a.so",)), "a.so": spec}, expected=error)
            self.assertEqual(r, [2])
        self.graph_run({"app": (None, ("A.so",)), "a.so": ("a.so", ())}, expected=1)

    def test_dependency_identity_alias_releases_extra_reference(self):
        a, r, scope, order = self.graph_run({"app": (None, ("a.so", "alias.so", "alias.so")),
             "a.so": (None, ()), "alias.so": (None, ())}, identities={"alias.so": 2})
        self.assertEqual(a, ["a.so", "alias.so"])
        self.assertEqual(scope, [1, 2])
        self.assertEqual(r, [2, 2])  # duplicate acquire, then retained mapping

    def test_dependency_object_limit_and_long_chain(self):
        defs = {"app": (None, ("lib0.so",))}
        for i in range(127):
            defs[f"lib{i}.so"] = (f"lib{i}.so", (f"lib{i+1}.so",) if i < 126 else ())
        a, r, scope, order = self.graph_run(defs)
        self.assertEqual(len(scope), 128)
        self.assertEqual(order, list(range(128, 0, -1)))
        defs["lib126.so"] = ("lib126.so", ("overflow.so",))
        a, r, _, _ = self.graph_run(defs, expected=-4)
        self.assertEqual(len(a), 127)
        self.assertEqual(len(r), 127)

    def test_real_x32_objects(self):
        leaf = self.path / "leaf.c"
        leaf.write_text((ROOT / "tests/ldso_reloc/leaf.c").read_text())
        library = self.path / "library.c"
        library.write_text((ROOT / "tests/ldso_reloc/library.c").read_text())
        program = self.path / "program.c"
        program.write_text("extern int answer(void);\nextern int value;\n"
                           "int _start(void) { return answer() + value; }\n")
        common = X32_CC + ["-mx32", "-ffreestanding", "-fno-stack-protector", "-nostdlib"]
        run(common + ["-fPIC", "-shared", "-Wl,--hash-style=sysv,-soname,libleaf.so",
                      str(leaf), "-o", str(self.path / "libleaf.so")])
        run(common + ["-fPIC", "-shared", "-Wl,--hash-style=sysv,-soname,libtest.so",
                      str(library), "-L" + str(self.path), "-lleaf",
                      "-o", str(self.path / "libtest.so")])
        # No PIE requirement: a fixed-address ET_EXEC consumes PIC DSOs.
        run(common + ["-fPIC", "-no-pie", "-Wl,--hash-style=sysv,-z,now",
                      "-Wl,--dynamic-linker,/GEMSYS/LIB/X32/LD.SO",
                      str(program), "-L" + str(self.path), "-ltest",
                      "-Wl,-rpath-link," + str(self.path),
                      "-o", str(self.path / "program.elf")])
        loaded = {}
        for filename, kind in [("libleaf.so", 3), ("libtest.so", 3), ("program.elf", 2)]:
            with self.subTest(filename=filename):
                raw = (self.path / filename).read_bytes()
                self.assertEqual(raw[:6], b"\x7fELF\x01\x01")
                fields = struct.unpack_from("<HHIIIIIHHHHHH", raw, 16)
                self.assertEqual(fields[:2], (kind, 62))  # ELF32 EM_X86_64
                phoff, phsize, phnum = fields[4], fields[8], fields[9]
                headers = [struct.unpack_from("<8I", raw, phoff + i * phsize) for i in range(phnum)]
                loads = [h for h in headers if h[0] == 1]
                base = min(h[2] for h in loads)
                end = max(h[2] + h[5] for h in loads)
                image = bytearray(end - base)
                for h in loads:
                    image[h[2] - base:h[2] - base + h[4]] = raw[h[1]:h[1] + h[4]]
                dynamic = next(h for h in headers if h[0] == 2)
                bias = {"libleaf.so": 0x3000000, "libtest.so": 0x2000000, "program.elf": 0}[filename]
                rc, plan, raw_buffer = self.image_read(raw, role=1 if kind == 3 else 3)
                self.assertEqual(rc, 0)
                self.assertEqual(plan.base, base)
                rc, obj, memory = self.image_copy(plan, raw_buffer, bias=bias)
                self.assertEqual(rc, 0)
                self.assertEqual(bytes(memory)[:len(image)], bytes(image))
                self.assertEqual(bytes(memory)[len(image):plan.size], bytes(plan.size - len(image)))
                loaded[filename] = (obj, memory, headers)
                tags = {}
                for offset in range(0, dynamic[4], 8):
                    tag, value = struct.unpack_from("<II", image, dynamic[2] - base + offset)
                    if not tag:
                        break
                    tags[tag] = value
                self.assertNotIn(22, tags)  # No DT_TEXTREL.
                if filename == "libleaf.so":
                    self.assertEqual(self.lookup(obj, b"leaf")[0], 0)
                elif filename == "libtest.so":
                    self.assertEqual(self.lookup(obj, b"answer")[0], 0)
                    self.assertEqual(self.lookup(obj, b"value")[0], 0)
                    self.assertEqual(self.lib.dl_string(C.byref(obj), obj.needed[0]), b"libleaf.so")
                    self.assertEqual(tags[27], 4)  # INIT_ARRAYSZ: one x32 pointer.
                    self.assertIn(25, tags)        # DT_INIT_ARRAY.
                    self.assertEqual(tags[9], 12)  # DT_RELAENT.
                    self.assertEqual(tags[20], 7)  # DT_PLTREL = DT_RELA.
                    types = set()
                    for address_tag, size_tag in ((7, 8), (23, 2)):
                        for offset in range(0, tags[size_tag], 12):
                            _, info, _ = struct.unpack_from("<IIi", image,
                                                           tags[address_tag] - base + offset)
                            types.add(info & 255)
                    self.assertEqual(types, {6, 7, 8})  # GLOB_DAT, JUMP_SLOT, RELATIVE.
                else:
                    interp = next(h for h in headers if h[0] == 3)
                    self.assertEqual(raw[interp[1]:interp[1] + interp[4]], b"/GEMSYS/LIB/X32/LD.SO\0")
                    self.assertEqual(self.lib.dl_string(C.byref(obj), obj.needed[0]), b"libtest.so")
                    self.assertTrue(tags[30] & 8)  # DF_BIND_NOW.

        # Resolve and apply the actual compiler-produced data/GOT/PLT and
        # constructor-pointer relocations, with distinct target load biases.
        acquired, released = [], []
        ids = {"program.elf": 1, "libtest.so": 2, "libleaf.so": 3}

        @Acquire
        def acquire(ctx, name, mapping):
            name = name.decode()
            acquired.append(name)
            if name not in loaded:
                return 1
            mapping[0] = Mapping(loaded[name][0], ids[name], ids[name])
            return 0

        @Release
        def release(ctx, mapping):
            released.append(mapping.contents.identity)

        graph, ops = Graph(), GraphOps(acquire, release)
        root = Mapping(loaded["program.elf"][0], 1, 1)
        self.assertEqual(self.lib.dl_graph_open(C.byref(graph), C.byref(root),
                                               C.byref(ops), None), 0)
        self.assertEqual(acquired, ["libtest.so", "libleaf.so"])
        self.assertEqual([graph.nodes[i].mapping.identity for i in range(graph.count)], [1, 2, 3])
        self.assertEqual(list(graph.order[:graph.order_count]), [2, 1, 0])
        scope = [graph.scope[i].contents for i in range(graph.count)]
        leaf = self.lookup(loaded["libleaf.so"][0], b"leaf")[2]
        answer = self.lookup(loaded["libtest.so"][0], b"answer")[2]
        value = self.lookup(loaded["libtest.so"][0], b"value")[2]
        for filename in ("libleaf.so", "libtest.so", "program.elf"):
            obj, memory, headers = loaded[filename]
            writable = [(h[2], h[5]) for h in headers if h[0] == 1 and h[6] & 2]
            rc, plan = self.relocate(obj, memory, objects=scope, ranges=writable)
            self.assertEqual(rc, 0)
            values = [w.value for w in plan]
            if filename == "libtest.so":
                self.assertIn(leaf, values)
                self.assertIn(value, values)
                self.assertGreaterEqual(len(plan), 3)
            elif filename == "program.elf":
                self.assertIn(answer, values)
                self.assertIn(value, values)
        obj, memory, _ = loaded["libtest.so"]
        self.assertEqual(struct.unpack_from("<I", memory, value - obj.bias - obj.link_base)[0], 5)
        self.lib.dl_graph_close(C.byref(graph), C.byref(ops), None)
        self.assertEqual(released, [3, 2])

    def test_dynamic_consumers_keep_embedded_console_static(self):
        import kconfiglib
        old_cwd = Path.cwd()
        old_prefix = os.environ.get("CONFIG_")
        try:
            os.chdir(ROOT)
            os.environ["CONFIG_"] = ""
            config = kconfiglib.Kconfig(warn=False)
            config.load_config("configs/pc-x86_64-desktop-tests_defconfig")
            for name in ("CONF_WITH_DYNAMIC_CLI", "CONF_WITH_DYNAMIC_DESK"):
                self.assertEqual(config.syms[name].tri_value, 0)
            config.syms["CONF_WITH_ELF_SHARED"].set_value(2)
            for name in ("CONF_WITH_DYNAMIC_CLI", "CONF_WITH_DYNAMIC_DESK"):
                self.assertEqual(config.syms[name].tri_value, 2)
            config.syms["CONF_EXTERNAL_CLI"].set_value(0)
            self.assertEqual(config.syms["CONF_WITH_DYNAMIC_CLI"].tri_value, 0)
            self.assertEqual(config.syms["CONF_WITH_DYNAMIC_DESK"].tri_value, 2)
        finally:
            os.chdir(old_cwd)
            if old_prefix is None:
                os.environ.pop("CONFIG_", None)
            else:
                os.environ["CONFIG_"] = old_prefix

    def test_kconfig_platform_gate(self):
        import kconfiglib

        old_cwd = Path.cwd()
        old_prefix = os.environ.get("CONFIG_")
        try:
            os.chdir(ROOT)
            os.environ["CONFIG_"] = ""
            for name, expected in [("pc-x86_64", 2), ("virt-arm", 0), ("atari512", 0)]:
                with self.subTest(config=name):
                    config = kconfiglib.Kconfig(warn=False)
                    config.load_config(str(ROOT / "configs" / (name + "_defconfig")))
                    option = config.syms["CONF_WITH_ELF_SHARED"]
                    self.assertEqual(option.tri_value, 0)
                    option.set_value(2)
                    self.assertEqual(option.tri_value, expected)
                    if expected:
                        self.assertEqual(config.syms["CONF_WITH_USER_ASPACE"].tri_value, 2)
                        self.assertEqual(config.syms["CONF_WITH_MMAP"].tri_value, 2)
                        option.set_value(0)
                        config.syms["CONF_WITH_MMAP"].set_value(2)
                        self.assertEqual(config.syms["CONF_WITH_MMAP"].tri_value, 2)
                    else:
                        config.syms["CONF_WITH_MMAP"].set_value(2)
                        self.assertEqual(config.syms["CONF_WITH_MMAP"].tri_value, 0)
        finally:
            os.chdir(old_cwd)
            if old_prefix is None:
                os.environ.pop("CONFIG_", None)
            else:
                os.environ["CONFIG_"] = old_prefix


if __name__ == "__main__":
    unittest.main(verbosity=2)
