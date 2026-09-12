#!/usr/bin/env python3
#
# rdb.py - client for Hatari's hrdb remote-debug protocol
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
"""Talks to a patched Hatari's remote-debug TCP port (56001) directly,
without the hrdb Qt GUI. Useful for scripted/automated debugging of pTOS's
m68k targets, and far more reliable than Hatari's own built-in debugger
(unreliable breakpoints, memory reads that lie -- see the "Hatari v2.5.0
debugger gotchas" section of .claude/skills/ptos-smoketest/SKILL.md).

Requires a Hatari build from the "hrdb-main" branch of
https://github.com/tattlemuss/hatari (a small, actively-maintained fork
adding this protocol; upstream/stock Hatari does not have it):

    git clone --branch hrdb-main --depth 1 https://github.com/tattlemuss/hatari
    cd hatari && mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
    # binary at build/src/hatari -- run it exactly like stock Hatari

The remote-debug port is always active once Hatari is running (no flag
needed); the emulator runs freely until a client connects and sends
"break". Protocol source: src/debug/remotedebug.c in that repo -- plain
text, NUL-terminated commands/replies, 0x01-separated fields within a
reply, unsolicited notifications prefixed with "!" (e.g. "!status" when
a breakpoint is hit).

Usage:
    from rdb import RDB
    r = RDB()                    # connect to 127.0.0.1:56001
    r.bp("pc=$fc23d8")           # standard Hatari breakpoint expression
    r.run()
    stop = r.wait_stopped()      # blocks until the breakpoint fires
    regs = r.regs()              # {"D0": ..., "PC": ..., "SR": ..., ...}
    data, addr = r.mem(regs["A0"], 128)

Run this file directly for a quick connectivity smoke test against an
already-running Hatari instance.
"""
import socket
import time

SEP = b"\x01"


class RDB:
    def __init__(self, host="127.0.0.1", port=56001, timeout=10):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.timeout = timeout  # restored after each call's own temporary timeout
        self.buf = bytearray()
        self._scanned = 0  # buf[:_scanned] is known NUL-free; skip rescanning it
        # Drain the initial handshake + notifications (!connected, !config, !status, !symbols)
        self.notifications = []
        self._drain_notifications()

    def _read_msg(self):
        idx = self.buf.find(b"\x00", self._scanned)
        while idx < 0:
            self._scanned = len(self.buf)
            data = self.sock.recv(65536)
            if not data:
                raise ConnectionError("socket closed")
            self.buf += data  # bytearray += extends in place, unlike bytes +=
            idx = self.buf.find(b"\x00", self._scanned)
        msg = bytes(self.buf[:idx])
        del self.buf[:idx + 1]
        self._scanned = 0
        return msg

    def _drain_notifications(self, timeout=5):
        # RemoteDebugState_TryAccept() always sends exactly these four on
        # connect, in this order (see remotedebug.c): !connected, !config,
        # !status, !symbols. Read until the last one, rather than an idle
        # timeout, since the whole burst can take a while to arrive.
        self.sock.settimeout(timeout)
        try:
            while True:
                msg = self._read_msg()
                self.notifications.append(msg)
                if msg.startswith(b"!symbols"):
                    return
        except socket.timeout:
            raise RuntimeError(
                "timed out waiting for the '!symbols' connect handshake "
                f"(got so far: {self.notifications!r}) -- is this really "
                "the hrdb-patched Hatari's remote-debug port, not stock "
                "Hatari or something else entirely?"
            ) from None
        finally:
            self.sock.settimeout(self.timeout)

    def send_cmd(self, cmd):
        """Send one command, return its reply (any '!' notifications seen
        while waiting are stashed in self.notifications instead).

        RemoteDebug_ProcessBuffer() unconditionally sends a second, empty
        NUL-terminated message after every real reply (the command handler
        already sent its own terminator, then the dispatcher sends one more)
        -- skip those too, since a real reply is never empty."""
        self.sock.sendall(cmd.encode() + b"\x00")
        while True:
            msg = self._read_msg()
            if not msg or msg.startswith(b"!"):
                if msg:
                    self.notifications.append(msg)
                continue
            return msg

    def parts(self, msg):
        return msg.split(SEP)

    # --- convenience wrappers -------------------------------------------------
    def status(self):
        p = self.parts(self.send_cmd("status"))
        return {"ok": p[0] == b"OK", "running": int(p[1], 16), "pc": int(p[2], 16)}

    def regs(self):
        p = self.parts(self.send_cmd("regs"))
        if p[0] != b"OK":
            raise RuntimeError(f"regs failed: {p[:3]!r}")
        d = {}
        i = 2  # p[1] is an empty field (send_key_value's own leading separator)
        while i + 1 < len(p):
            d[p[i].decode()] = int(p[i + 1], 16)
            i += 2
        return d

    def mem(self, addr, count):
        """Returns (bytes, actual_start_addr). Server encodes 3 bytes as 4
        chars offset by 32 (a uuencode-like scheme), not base64/hex."""
        msg = self.send_cmd(f"mem {addr:x} {count:x}")
        p = msg.split(SEP, 3)
        if len(p) < 4 or p[0] != b"OK":
            raise RuntimeError(f"mem failed: {msg!r}")
        raddr = int(p[1], 16)
        rcount = int(p[2], 16)
        enc = p[3]
        out = bytearray()
        for i in range(0, len(enc), 4):
            chunk = enc[i:i + 4]
            if len(chunk) < 4:
                break
            accum = 0
            for c in chunk:
                accum = (accum << 6) | ((c - 32) & 0x3F)
            out += bytes([(accum >> 16) & 0xFF, (accum >> 8) & 0xFF, accum & 0xFF])
        return bytes(out[:rcount]), raddr

    def bp(self, expr):
        """expr uses Hatari's standard breakpoint-condition syntax, e.g.
        "pc=$fc23d8" or "pc=$fc23d8 && d0=0"."""
        return self.send_cmd(f"bp {expr}")

    def bplist(self):
        return self.send_cmd("bplist")

    def bpdel(self, idx):
        return self.send_cmd(f"bpdel {idx:x}")

    def step(self):
        return self.send_cmd("step")

    def run(self):
        return self.send_cmd("run")

    def break_(self):
        return self.send_cmd("break")

    def wait_stopped(self, timeout=60):
        """Block until a "!status" notification reports execution stopped
        (e.g. a breakpoint fired), and return {"pc": ...}."""
        deadline = time.time() + timeout
        self.sock.settimeout(1)
        try:
            while time.time() < deadline:
                try:
                    msg = self._read_msg()
                except socket.timeout:
                    continue
                if msg.startswith(b"!status"):
                    p = self.parts(msg)
                    running = int(p[1], 16)
                    if running == 0:
                        return {"pc": int(p[2], 16)}
                else:
                    self.notifications.append(msg)
        finally:
            self.sock.settimeout(self.timeout)
        raise TimeoutError("timed out waiting for stop")


if __name__ == "__main__":
    r = RDB()
    print("notifications on connect:")
    for n in r.notifications:
        print(" ", n)
    print("status:", r.status())
    regs = r.regs()
    print("PC=%08X SR=%04X D0=%08X A0=%08X" % (regs["PC"], regs["SR"], regs["D0"], regs["A0"]))
