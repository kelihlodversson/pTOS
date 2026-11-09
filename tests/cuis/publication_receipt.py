"""Executable REVIEW MODEL, not a CUIS handler or native ownership proof.

Models only bounded opaque stream publication and the libaes recovery ordering.
PROCESS completion/reaping and scheduler behavior deliberately do not live here.
"""
from dataclasses import dataclass, field
import struct
import threading
import unittest

MAX_GENERATION = (1 << 64) - 1

class Rejected(Exception):
    pass

class ThreadLost(Exception):
    """Injected publisher THREAD loss; its PROCESS/client survives."""

@dataclass(frozen=True)
class Endpoint:
    handle: int
    generation: int

@dataclass
class Client:
    owner: int
    live: bool = True
    capacity: int = 64
    packets: list = field(default_factory=list)
    appends: int = 0

@dataclass
class Receipt:
    generation: int = 0
    state: str = 'RECLAIMED'
    key: int = 0
    destination: Endpoint = None
    packet: bytes = b''
    committed: bool = False

class Stream:
    """Each method is one modeled serialized transport operation."""
    def __init__(self, slots=16, quota=256):
        self.slots, self.quota = slots, quota
        self.clients, self.tables = {}, {}
        self.lock = threading.Lock()

    def register(self, endpoint, owner, capacity=64):
        if endpoint in self.clients:
            raise Rejected('incarnation reuse')
        self.clients[endpoint] = Client(owner, capacity=capacity)
        self.tables[endpoint] = [Receipt() for _ in range(self.slots)]

    def _source(self, owner, source):
        client = self.clients.get(source)
        if not client or not client.live or client.owner != owner:
            raise Rejected('source authority')
        return client

    def _record(self, source, slot, generation):
        if not 0 <= slot < self.slots:
            raise Rejected('slot')
        receipt = self.tables[source][slot]
        if generation != receipt.generation or not generation:
            raise Rejected('stale attempt')
        return receipt

    def prepare(self, owner, source, slot, generation, key, destination, packet,
                fail_copy=False, fail_reserve=False):
        with self.lock:
            self._source(owner, source)
            if not 0 <= slot < self.slots or not key or not 0 < len(packet) <= 32:
                raise Rejected('bounds')
            r = self.tables[source][slot]
            if r.generation == generation and r.state != 'RECLAIMED':
                if (r.key, r.destination, r.packet) != (key, destination, packet):
                    raise Rejected('immutable attempt')
                return
            if r.state != 'RECLAIMED' or generation != r.generation + 1 or generation > MAX_GENERATION:
                raise Rejected('stale/busy/exhausted attempt')
            if any(x.key == key and x.state != 'RECLAIMED' for x in self.tables[source]):
                raise Rejected('key already bound: recover existing slot')
            peer = self.clients.get(destination)
            if not peer or not peer.live:
                raise Rejected('destination incarnation')
            active = sum(x.state != 'RECLAIMED' for table in self.tables.values() for x in table)
            if fail_copy or fail_reserve or active >= self.quota:
                raise Rejected('prepublication failure')
            # All staging resources are reserved before making PREPARED durable.
            self.tables[source][slot] = Receipt(generation, 'PREPARED', key, destination, bytes(packet))

    def query(self, owner, source, slot, generation):
        with self.lock:
            self._source(owner, source)
            r = self._record(source, slot, generation)
            return r.state, r.committed, r.key, r.destination, r.packet

    def commit(self, owner, source, slot, generation, lose_thread=False):
        with self.lock:
            self._source(owner, source)
            r = self._record(source, slot, generation)
            if r.state in ('COMMITTED', 'ACKNOWLEDGED'):
                return len(r.packet)  # Never inspect queued bytes to recover.
            if r.state != 'PREPARED':
                raise Rejected('reclaimed: never republish')
            peer = self.clients.get(r.destination)
            if not peer or not peer.live:
                raise Rejected('destination retired')
            if sum(len(data) for _, data in peer.packets) + len(r.packet) > peer.capacity:
                raise Rejected('EAGAIN')
            # The proposed single bounded non-yielding commit boundary.
            peer.packets.append((source, r.packet))
            peer.appends += 1
            r.state, r.committed = 'COMMITTED', True
        if lose_thread:
            raise ThreadLost('after stream+receipt commit, before syscall return')
        return len(r.packet)

    def ack(self, owner, source, slot, generation, lose_thread=False):
        with self.lock:
            self._source(owner, source)
            r = self._record(source, slot, generation)
            if r.state not in ('COMMITTED', 'ACKNOWLEDGED'):
                raise Rejected('not committed')
            r.state = 'ACKNOWLEDGED'  # Identity is NOT reclaimed by ACK.
        if lose_thread:
            raise ThreadLost('after durable ACK, before result reaches userspace')

    def reclaim(self, owner, source, slot, generation):
        with self.lock:
            self._source(owner, source)
            r = self._record(source, slot, generation)
            if r.state not in ('PREPARED', 'ACKNOWLEDGED', 'RECLAIMED'):
                raise Rejected('must ACK committed receipt first')
            r.state = 'RECLAIMED'
            # Last generation/outcome/key remain a bounded stale-attempt fence.
            r.packet = b''

    def consume(self, endpoint):
        with self.lock:
            result = self.clients[endpoint].packets[:]
            self.clients[endpoint].packets.clear()
            return result

    def retire(self, endpoint):
        with self.lock:
            self.clients[endpoint].live = False
            self.tables.pop(endpoint)  # Source-owned table; never retarget peer.

class Adapter:
    """One immutable binding, shared by surviving THREADs of a libaes client.

    The binding is installed before child COMMIT. It is not a THREAD-local
    progress flag. Reuse is forbidden until all observer aliases/leases settle.
    """
    def __init__(self, stream, owner, source, destination, slot=0, generation=1, key=91):
        self.s, self.owner, self.source, self.destination = stream, owner, source, destination
        self.slot, self.generation, self.key = slot, generation, key
        self.right_open = True
        self.leases = 0
        self.packet = struct.pack('<8H', 90, 0, 0, 42, 0xffff, 0, 0, 0)

    @property
    def args(self):
        return self.owner, self.source, self.slot, self.generation

    def prepare(self, **failures):
        self.s.prepare(*self.args, self.key, self.destination, self.packet, **failures)

    def finish(self, stop_after_ack=False, stop_after_close=False, close_error=False):
        state, committed, key, destination, _ = self.s.query(*self.args)
        assert key == self.key and destination == self.destination
        if state == 'PREPARED':
            self.s.commit(*self.args)
            committed = True
        assert committed, 'no false committed delivery'
        if state != 'RECLAIMED':
            self.s.ack(*self.args, lose_thread=stop_after_ack)
        # CLOSE consumes the PROCESS right even if it reports cleanup error.
        self.right_open = False
        if stop_after_close:
            raise ThreadLost('after consumed CLOSE')
        assert not self.right_open and not self.leases
        self.s.reclaim(*self.args)
        return 'cleanup error, consumed' if close_error else 'delivered'

class ReceiptTests(unittest.TestCase):
    def setUp(self):
        self.source, self.destination = Endpoint(7, 11), Endpoint(8, 12)
        self.s = Stream()
        self.s.register(self.source, 101)
        self.s.register(self.destination, 202)
        self.a = Adapter(self.s, 101, self.source, self.destination)

    def once(self):
        self.assertEqual(self.s.clients[self.destination].appends, 1)

    def test_fault_after_commit_receiver_consumes_sibling_recovers(self):
        self.a.prepare()
        with self.assertRaises(ThreadLost): self.s.commit(*self.a.args, lose_thread=True)
        self.assertEqual(self.s.consume(self.destination), [(self.source, self.a.packet)])
        # No userspace success flag was ever written by the lost THREAD.
        self.assertEqual(self.a.finish(), 'delivered')
        self.once()

    def test_lost_prepare_result_recovers_known_attempt(self):
        self.a.prepare()  # THREAD loss/result copy fault after admission.
        self.assertEqual(self.s.query(*self.a.args)[:2], ('PREPARED', False))
        self.a.prepare()  # Same immutable binding; no new attempt.
        self.a.finish(); self.once()

    def test_staged_packet_cannot_change_with_user_buffer(self):
        packet = bytearray(self.a.packet)
        self.s.prepare(*self.a.args, self.a.key, self.destination, packet)
        packet[0] ^= 255
        self.a.finish()
        self.assertEqual(self.s.consume(self.destination), [(self.source, self.a.packet)])

    def test_ack_preflight_failure_keeps_committed_outcome(self):
        self.a.prepare(); self.s.commit(*self.a.args)
        with self.assertRaises(Rejected): self.s.ack(999, self.source, 0, 1)
        self.assertEqual(self.s.query(*self.a.args)[:2], ('COMMITTED', True))
        with self.assertRaises(Rejected): self.s.reclaim(*self.a.args)
        self.a.finish(); self.once()

    def test_pending_cancellation_before_recording_success_same_recovery(self):
        self.a.prepare(); self.s.commit(*self.a.args)
        self.s.consume(self.destination)
        self.a.finish(); self.once()

    def test_concurrent_commit_retries_append_once(self):
        self.a.prepare(); barrier = threading.Barrier(3); results = []
        def commit():
            barrier.wait(); results.append(self.s.commit(*self.a.args))
        peers = [threading.Thread(target=commit) for _ in range(2)]
        for peer in peers: peer.start()
        barrier.wait()
        for peer in peers: peer.join()
        self.assertEqual(results, [16, 16]); self.once()

    def test_ack_loss_then_consumed_close_error_then_reclaim(self):
        self.a.prepare(); self.s.commit(*self.a.args)
        with self.assertRaises(ThreadLost): self.a.finish(stop_after_ack=True)
        self.s.consume(self.destination)
        self.assertEqual(self.s.query(*self.a.args)[0], 'ACKNOWLEDGED')
        with self.assertRaises(ThreadLost): self.a.finish(stop_after_close=True)
        self.assertFalse(self.a.right_open)
        self.assertEqual(self.a.finish(close_error=True), 'cleanup error, consumed')
        self.a.finish(); self.once()

    def test_ack_before_commit_fails_without_false_receipt(self):
        self.a.prepare()
        with self.assertRaises(Rejected): self.s.ack(*self.a.args)
        self.assertEqual(self.s.query(*self.a.args)[:2], ('PREPARED', False))

    def test_stream_full_leaves_prepared_retryable(self):
        self.a.prepare(); self.s.clients[self.destination].capacity = 0
        with self.assertRaises(Rejected): self.s.commit(*self.a.args)
        self.assertEqual(self.s.query(*self.a.args)[:2], ('PREPARED', False))
        self.s.clients[self.destination].capacity = 64
        self.a.finish(); self.once()

    def test_copy_allocation_quota_failures_before_publication(self):
        for failure in ('fail_copy', 'fail_reserve'):
            with self.assertRaises(Rejected): self.a.prepare(**{failure: True})
            self.assertEqual(self.s.tables[self.source][0].generation, 0)
        self.s.quota = 0
        with self.assertRaises(Rejected): self.a.prepare()
        self.assertEqual(self.s.clients[self.destination].appends, 0)

    def test_immutable_reprepare_and_duplicate_key(self):
        self.a.prepare(); self.a.prepare()
        with self.assertRaises(Rejected):
            self.s.prepare(*self.a.args, self.a.key, self.destination, b'changed')
        with self.assertRaises(Rejected):
            self.s.prepare(101, self.source, 1, 1, self.a.key, self.destination, self.a.packet)
        self.a.finish(); self.once()

    def test_foreign_owner_and_endpoint_generation(self):
        self.a.prepare()
        with self.assertRaises(Rejected): self.s.commit(999, self.source, 0, 1)
        with self.assertRaises(Rejected): self.s.query(101, Endpoint(7, 10), 0, 1)

    def test_destination_retirement_never_retargets_reused_handle(self):
        self.a.prepare(); self.s.retire(self.destination)
        replacement = Endpoint(8, 13); self.s.register(replacement, 303)
        with self.assertRaises(Rejected): self.s.commit(*self.a.args)
        self.assertEqual(self.s.clients[replacement].appends, 0)
        self.s.reclaim(*self.a.args)

    def test_committed_receipt_survives_destination_retirement(self):
        self.a.prepare(); self.s.commit(*self.a.args); self.s.retire(self.destination)
        self.a.finish(); self.once()

    def test_source_retirement_reclaims_all_unacked_states(self):
        for committed in (False, True):
            self.setUp(); self.a.prepare()
            if committed: self.s.commit(*self.a.args)
            self.s.retire(self.source)
            replacement = Endpoint(7, 12); self.s.register(replacement, 101)
            with self.assertRaises(Rejected): self.s.query(*self.a.args)
            self.assertNotIn(self.source, self.s.tables)
            self.assertEqual(self.s.clients[self.destination].appends, int(committed))

    def test_reclaimed_and_stale_attempts_never_append(self):
        self.a.prepare(); self.a.finish()
        self.assertEqual(self.s.query(*self.a.args)[:2], ('RECLAIMED', True))
        with self.assertRaises(Rejected): self.s.commit(*self.a.args)
        self.s.prepare(101, self.source, 0, 2, 92, self.destination, b'next')
        with self.assertRaises(Rejected): self.s.commit(*self.a.args)
        with self.assertRaises(Rejected): self.s.ack(*self.a.args)
        self.once()

    def test_bounded_slots_and_generation_exhaustion(self):
        self.s.tables[self.source][0].generation = MAX_GENERATION
        with self.assertRaises(Rejected): self.a.prepare()
        with self.assertRaises(Rejected):
            self.s.prepare(101, self.source, 16, 1, 99, self.destination, b'x')
        self.assertEqual(len(self.s.tables[self.source]), 16)

    def test_wire_layout_both_byte_orders(self):
        for endian in ('<', '>'):
            wire = struct.Struct(endian + '16I')
            self.assertEqual(wire.size, 64)
            values = tuple(range(16)); self.assertEqual(wire.unpack(wire.pack(*values)), values)
            self.assertEqual(struct.unpack(endian+'I', wire.pack(*values)[48:52])[0], 12)

    def test_negative_control_write_then_mark_duplicates_after_drain(self):
        # Unsafe original proposal: no durable receipt, receiver drains packet,
        # publisher dies before userspace success flag; sibling writes again.
        self.a.prepare(); self.s.commit(*self.a.args); self.s.consume(self.destination)
        self.s.tables[self.source][0] = Receipt(1, 'PREPARED', self.a.key,
                                               self.destination, self.a.packet)
        self.s.commit(*self.a.args)
        self.assertEqual(self.s.clients[self.destination].appends, 2)
        with self.assertRaises(AssertionError): self.once()

    def test_negative_control_early_reuse_before_close_duplicates_delivery(self):
        self.a.prepare(); self.s.commit(*self.a.args); self.s.consume(self.destination)
        self.s.ack(*self.a.args)
        # Invalid adapter: erase/reuse ACK state while old PROCESS right lives.
        self.s.reclaim(*self.a.args)
        self.assertTrue(self.a.right_open)
        self.s.prepare(101, self.source, 0, 2, self.a.key, self.destination, self.a.packet)
        self.s.commit(101, self.source, 0, 2)
        self.assertEqual(self.s.clients[self.destination].appends, 2)
        with self.assertRaises(AssertionError): self.once()

if __name__ == '__main__':
    unittest.main(verbosity=2)
