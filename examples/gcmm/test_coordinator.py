"""Offline GCMM coordinator simulations and Source RCON framing tests.

Run: python -m unittest discover -s examples/gcmm -p 'test_*.py' -v
No game, server, socket connection, or credentials are needed for this suite.
"""
from __future__ import annotations

import copy
import json
import struct
import threading
import unittest
from unittest.mock import patch

import coordinator as gcmm


def arguments(command):
    return dict(token.split("=", 1) for token in command.split()[1:])


def ok(role, **fields):
    return {"protocol": "gcmm/1", "role": role, **fields}


class Clock:
    def __init__(self):
        self.value = 0.0
        self.sleeps = 0
        self.on_sleep = lambda: None

    def now(self):
        return self.value

    def sleep(self, interval):
        self.value += interval
        self.sleeps += 1
        self.on_sleep()


class Server:
    label, role = "server@simulation", "server"

    def __init__(self):
        self.lock = threading.RLock()
        self.cookie = self.match = self.request = 0
        self.roster = {}
        self.active = False
        self.last_result = False
        self.releases = []
        self.reserves = []
        self.lie_all_accepted = False
        self.lose_reserve_reply = False
        self.initialized = self.enabled = True
        self.defer_release = self.pending_release = False
        self.before_status = lambda: None

    def command(self, command):
        with self.lock:
            name = command.split()[0]
            args = arguments(command)
            if name == "gcmm_status":
                self.before_status()
                stages = list(self.roster.values())
                return ok("server", enabled=self.enabled, initialized=self.initialized,
                          request_id=str(self.request), last_result=self.last_result,
                          server_steamid="1001", cookie=f"0x{self.cookie:016x}",
                          match_id=f"0x{self.match:016x}", queue="Q" if self.active else "",
                          roster_count=len(stages), expected_humans=len(stages),
                          minimum_stage=min(stages, default=0), accepted=sum(s >= 2 for s in stages),
                          matches_request=self.active, active=self.active,
                          all_accepted=self.lie_all_accepted or bool(stages) and all(s >= 2 for s in stages),
                          roster=[{"account_id": account, "stage": stage}
                                  for account, stage in self.roster.items()])
            if name == "gcmm_reserve":
                self.cookie, self.match = int(args["reservationid"], 0), int(args["match_id"], 0)
                self.roster = {int(account): 0 for account in args["accounts"].split(",")}
                self.active = self.last_result = True
                self.request += 1
                self.reserves.append(copy.deepcopy(args))
                if self.lose_reserve_reply:
                    raise gcmm.GCMMError("lost reserve response after native mutation")
                return ok("server", request_id=str(self.request))
            if name == "gcmm_release":
                if (int(args["reservationid"], 0), int(args["match_id"], 0)) != (self.cookie, self.match):
                    raise gcmm.StaleOwner("release must not clear another reservation")
                self.releases.append((self.cookie, self.match))
                if self.defer_release:
                    self.pending_release = True
                else:
                    self.cookie = self.match = 0
                    self.roster = {}
                    self.active = False
                self.request += 1
                return ok("server", request_id=str(self.request))
            raise AssertionError(f"unexpected server command {name}")


class Client:
    role = "client"

    def __init__(self, account, server):
        self.label = f"client@simulation-{account}"
        self.account = account
        self.server = server
        self.search = 1
        self.phase = "searching"
        self.reservation = None
        self.party = [account]
        self.game_type = 462552584  # Real 1575 request; low nibble 8, not offer 4104.
        self.version = 13881
        self.prepares = []
        self.commits = []
        self.cancels = []
        self.fail_prepare = self.lose_prepare_reply = self.fail_commit = False
        self.prepare_barrier = None

    def command(self, command):
        name = command.split()[0]
        args = arguments(command)
        if name == "gcmm_status":
            return ok("client", enabled=True, steam_id=str(76561197960265728 + self.account),
                      account_id=self.account, search_id=str(self.search), phase=self.phase,
                      request={"account_ids": self.party, "game_type": self.game_type,
                               "client_version": self.version}, reservation=copy.deepcopy(self.reservation))
        if int(args["search_id"]) != self.search:
            raise gcmm.StaleOwner("client rejects stale search id")
        if name == "gcmm_reserve":
            if self.prepare_barrier:
                self.prepare_barrier.wait(timeout=3)
            if self.fail_prepare:
                raise gcmm.ReplyError("invalid_request", "simulated prepare failure")
            self.prepares.append(copy.deepcopy(args))
            self.reservation = {
                "reservationid": args["reservationid"], "match_id": args["match_id"],
                "accounts": [int(account) for account in args["accounts"].split(",")],
                "ip": args["ip"], "port": int(args["port"]), "map": args["map"],
                "game_type": int(args["game_type"]), "serverid": args["serverid"],
            }
            self.phase = "reserved"
            with self.server.lock:
                self.server.roster[self.account] = 1  # Simulated native stage-1 request.
            if self.lose_prepare_reply:
                raise gcmm.GCMMError("lost prepare response after client mutation")
            return ok("client", phase=self.phase)
        if name == "gcmm_commit":
            with self.server.lock:
                assert self.server.active
                assert len(self.server.roster) == len(self.reservation["accounts"])
                assert all(stage >= 2 for stage in self.server.roster.values()), "committed before last native ACCEPT"
                assert int(args["reservationid"], 0) == self.server.cookie
                assert int(args["match_id"], 0) == self.server.match
            self.commits.append(copy.deepcopy(args))
            self.phase = "committed"
            if self.fail_commit:
                raise gcmm.GCMMError("lost commit reply after mutation")
            return ok("client", phase=self.phase)
        if name == "gcmm_cancel":
            self.cancels.append(self.search)
            self.phase, self.reservation = "idle", None
            return ok("client", phase=self.phase)
        raise AssertionError(f"unexpected client command {name}")


class CoordinatorTests(unittest.TestCase):
    def setUp(self):
        self.server = Server()
        self.clients = [Client(200001 + index, self.server) for index in range(10)]
        self.clock = Clock()
        self.identifiers = iter([0, 0x1122334455667788, 0x0A0B0C0D])

    def coordinator(self, players=10, **overrides):
        options = dict(game_address=("203.0.113.121", 27016), map_name="de_dust2", players=players,
                       poll_interval=0.25, prepare_timeout=3, accept_timeout=2,
                       clock=self.clock.now, sleep=self.clock.sleep,
                       id_factory=lambda: next(self.identifiers))
        options.update(overrides)
        return gcmm.Coordinator(self.server, self.clients, **options)

    def accept_all(self):
        with self.server.lock:
            for account in self.server.roster:
                self.server.roster[account] = 2

    def test_ten_real_clients_prepare_concurrently_and_wait_for_last_accept(self):
        barrier = threading.Barrier(10)
        for client in self.clients:
            client.prepare_barrier = barrier
        self.server.lie_all_accepted = True  # Boolean alone must never allow commit.

        def progress():
            self.assertTrue(all(not client.commits for client in self.clients))
            with self.server.lock:
                for client in self.clients[:-1]:
                    self.server.roster[client.account] = 2
                if self.clock.sleeps >= 3:
                    self.server.roster[self.clients[-1].account] = 2
        self.clock.on_sleep = progress
        transaction = self.coordinator().run_once(search_timeout=1)
        self.assertEqual(self.clock.sleeps, 3)
        self.assertTrue(all(len(client.commits) == 1 for client in self.clients))
        selected = {client.account for client in self.clients}
        self.assertEqual(set(self.server.roster), selected)
        self.assertEqual(len(transaction.clients), 10)
        self.assertEqual(self.server.releases, [])  # Native clients still need this reservation.
        for client in self.clients:
            self.assertEqual(set(map(int, client.prepares[0]["accounts"].split(","))), selected)
            self.assertEqual(client.prepares[0]["game_type"], "4104")
            self.assertEqual(client.commits[0]["search_id"], "1")

    def test_default_requires_ten_real_clients_and_one_is_explicit_smoke(self):
        with self.assertRaisesRegex(ValueError, "at least --players"):
            gcmm.Coordinator(self.server, self.clients[:1], game_address=("127.0.0.1", 27016), map_name="de_dust2")
        self.clock.on_sleep = self.accept_all
        transaction = self.coordinator(players=1).run_once(search_timeout=1)
        self.assertEqual(len(transaction.clients), 1)
        self.assertEqual(set(self.server.roster), {self.clients[0].account})

    def test_party_fails_before_reserving_anything(self):
        self.clients[0].party.append(999999)
        with self.assertRaisesRegex(gcmm.GCMMError, "solo-party"):
            self.coordinator().run_once(search_timeout=1)
        self.assertEqual(self.server.reserves, [])
        self.assertTrue(all(not client.prepares for client in self.clients))

    def test_incompatible_modes_and_versions_fail_clearly(self):
        self.clients[0].game_type = 4105
        with self.assertRaisesRegex(gcmm.GCMMError, "competitive"):
            self.coordinator().discover()
        self.clients[0].game_type = 462552584
        self.clients[0].version = 1
        with self.assertRaisesRegex(gcmm.GCMMError, "version"):
            self.coordinator().discover()

    def test_duplicate_accounts_are_never_counted_as_distinct_people(self):
        self.clients[1].account = self.clients[0].account
        self.clients[1].party = [self.clients[0].account]
        with self.assertRaisesRegex(gcmm.GCMMError, "same real account"):
            self.coordinator().discover()
        self.assertEqual(self.server.reserves, [])

    def test_idle_clients_do_not_get_fabricated_or_reserved(self):
        self.clients[-1].phase, self.clients[-1].search = "idle", 0
        with self.assertRaisesRegex(gcmm.GCMMError, "required real searching"):
            self.coordinator().run_once(search_timeout=0.5)
        self.assertEqual(self.server.reserves, [])

    def test_cancellation_aborts_without_any_commit(self):
        def cancel():
            self.clients[0].phase, self.clients[0].reservation = "idle", None
        self.clock.on_sleep = cancel
        with self.assertRaises(gcmm.StaleOwner):
            self.coordinator().run_once(search_timeout=1)
        self.assertTrue(all(not client.commits for client in self.clients))
        self.assertEqual(len(self.server.releases), 1)
        self.assertTrue(all(client.phase == "idle" for client in self.clients))

    def test_stale_search_cleanup_never_cancels_the_new_search(self):
        def new_search():
            self.clients[0].search += 1
            self.clients[0].phase, self.clients[0].reservation = "searching", None
        self.clock.on_sleep = new_search
        with self.assertRaises(gcmm.StaleOwner):
            self.coordinator().run_once(search_timeout=1)
        self.assertEqual(self.clients[0].cancels, [])
        self.assertEqual(self.clients[0].phase, "searching")
        self.assertEqual(len(self.server.releases), 1)

    def test_changed_server_owner_is_never_released(self):
        def other_owner():
            self.server.cookie += 1
            self.server.match += 1
            self.server.request += 1
        self.clock.on_sleep = other_owner
        with self.assertRaises(gcmm.StaleOwner):
            self.coordinator().run_once(search_timeout=1)
        self.assertEqual(self.server.releases, [])
        self.assertTrue(self.server.active)
        self.assertTrue(all(client.phase == "idle" for client in self.clients))

    def test_partially_prepared_failure_cleans_all_attempted_owners(self):
        self.clients[3].fail_prepare = True
        with self.assertRaises(gcmm.ReplyError):
            self.coordinator().run_once(search_timeout=1)
        self.assertTrue(all(client.phase == "idle" for client in self.clients))
        self.assertTrue(all(not client.commits for client in self.clients))
        self.assertEqual(len(self.server.releases), 1)

    def test_lost_client_prepare_reply_still_cleans_actual_owned_reservation(self):
        self.clients[3].lose_prepare_reply = True
        with self.assertRaisesRegex(gcmm.GCMMError, "lost prepare"):
            self.coordinator().run_once(search_timeout=1)
        self.assertTrue(all(client.phase == "idle" for client in self.clients))
        self.assertEqual(len(self.server.releases), 1)

    def test_lost_server_reserve_reply_releases_its_actual_owner_only(self):
        self.server.lose_reserve_reply = True
        with self.assertRaisesRegex(gcmm.GCMMError, "lost reserve"):
            self.coordinator().run_once(search_timeout=1)
        self.assertEqual(len(self.server.releases), 1)
        self.assertTrue(all(not client.prepares and not client.cancels for client in self.clients))

    def test_timeout_never_auto_accepts_and_cleans_transaction(self):
        with self.assertRaisesRegex(gcmm.GCMMError, "native ACCEPT"):
            self.coordinator().run_once(search_timeout=1)
        self.assertTrue(all(not client.commits for client in self.clients))
        self.assertEqual(len(self.server.releases), 1)
        self.assertTrue(all(client.phase == "idle" for client in self.clients))

    def test_keyboard_interrupt_cleans_an_active_transaction(self):
        def interrupt():
            raise KeyboardInterrupt
        self.clock.on_sleep = interrupt
        with self.assertRaises(KeyboardInterrupt):
            self.coordinator().run_once(search_timeout=1)
        self.assertEqual(len(self.server.releases), 1)
        self.assertTrue(all(client.phase == "idle" for client in self.clients))

    def test_last_result_false_cannot_activate_or_commit_a_reservation(self):
        def unsuccessful():
            if self.server.request:
                self.server.last_result = False
        self.server.before_status = unsuccessful
        with self.assertRaisesRegex(gcmm.GCMMError, "native server reservation"):
            self.coordinator().run_once(search_timeout=1)
        self.assertTrue(all(not client.prepares and not client.commits for client in self.clients))
        self.assertEqual(len(self.server.releases), 1)

    def test_final_readiness_regression_prevents_commit(self):
        reads = [0]
        def regress():
            if self.server.roster and all(stage >= 2 for stage in self.server.roster.values()):
                reads[0] += 1
                if reads[0] == 2:
                    self.server.roster[self.clients[-1].account] = 1
        self.server.before_status = regress
        self.clock.on_sleep = self.accept_all
        with self.assertRaisesRegex(gcmm.GCMMError, "immediately before commit"):
            self.coordinator().run_once(search_timeout=1)
        self.assertTrue(all(not client.commits for client in self.clients))
        self.assertEqual(len(self.server.releases), 1)

    def test_partial_commit_failure_cancels_committed_and_reserved_clients(self):
        self.clients[3].fail_commit = True
        self.clock.on_sleep = self.accept_all
        with self.assertRaisesRegex(gcmm.GCMMError, "lost commit"):
            self.coordinator().run_once(search_timeout=1)
        self.assertTrue(all(client.phase == "idle" for client in self.clients))
        self.assertEqual(len(self.server.releases), 1)

    def test_cleanup_waits_for_native_release_to_apply(self):
        self.clients[3].fail_prepare = True
        self.server.defer_release = True
        def apply_release():
            if self.server.pending_release:
                self.server.cookie = self.server.match = 0
                self.server.roster, self.server.active = {}, False
                self.server.pending_release = False
        self.clock.on_sleep = apply_release
        with self.assertRaises(gcmm.ReplyError):
            self.coordinator().run_once(search_timeout=1)
        self.assertEqual(len(self.server.releases), 1)
        self.assertFalse(self.server.pending_release)
        self.assertFalse(self.server.active)
        self.assertEqual(self.clock.sleeps, 1)

    def test_cleanup_accepts_successful_inactive_release_with_retained_cookie(self):
        self.clients[3].fail_prepare = True
        self.server.defer_release = True
        def apply_release():
            if self.server.pending_release:
                self.server.active = False
                self.server.last_result = True
                self.server.pending_release = False
        self.clock.on_sleep = apply_release
        with self.assertRaises(gcmm.ReplyError):
            self.coordinator().run_once(search_timeout=1)
        self.assertEqual(len(self.server.releases), 1)
        self.assertFalse(self.server.active)
        self.assertNotEqual(self.server.cookie, 0)
        self.assertEqual(self.clock.sleeps, 1)

    def test_invalid_map_and_nonfinite_timeouts_fail_before_network(self):
        for options in [dict(map_name="de_dust2.bsp"), dict(map_name="a" * 129),
                        dict(accept_timeout=float("nan")), dict(poll_interval=float("inf")),
                        dict(server_version=0)]:
            with self.subTest(options=options), self.assertRaises(ValueError):
                self.coordinator(**options)
        self.assertEqual(self.server.reserves, [])

    def test_unsupported_native_adapter_fails_before_mutation(self):
        self.server.initialized = False
        with self.assertRaisesRegex(gcmm.GCMMError, "adapter unavailable"):
            self.coordinator().discover()
        self.assertEqual(self.server.reserves, [])


def packet(request_id, kind, text=""):
    body = struct.pack("<ii", request_id, kind) + text.encode() + b"\0\0"
    return struct.pack("<i", len(body)) + body


class FakeSocket:
    def __init__(self, data):
        self.data = bytearray(data)
        self.sent = []
        self.timeout = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def settimeout(self, value):
        self.timeout = value

    def recv(self, count):
        count = min(count, 3)  # Exercise fragmented TCP reads without using a socket.
        result = bytes(self.data[:count])
        del self.data[:count]
        return result

    def sendall(self, data):
        self.sent.append(data)


class RconTests(unittest.TestCase):
    def endpoint(self, data):
        connection = FakeSocket(data)
        endpoint = gcmm.SourceRconEndpoint(("127.0.0.1", 37016), "client", "TEST_SECRET",
                                           socket_factory=lambda *_: connection)
        return endpoint, connection

    def test_auth_two_packets_and_fragmented_response(self):
        data = packet(1, 0) + packet(1, 2) + packet(2, 0, "OK " + json.dumps(ok("client", enabled=True)))
        endpoint, connection = self.endpoint(data)
        self.assertTrue(endpoint.command("gcmm_status")["enabled"])
        self.assertEqual(len(connection.sent), 2)
        self.assertEqual(struct.unpack("<ii", connection.sent[0][4:12]), (1, 3))
        self.assertIn(b"gcmm_status", connection.sent[1])
        self.assertNotIn("TEST_SECRET", repr(endpoint))

    def test_auth_failure_never_sends_command(self):
        endpoint, connection = self.endpoint(packet(-1, 2))
        with self.assertRaises(gcmm.ReplyError) as caught:
            endpoint.command("gcmm_status")
        self.assertEqual(caught.exception.code, "authentication_failed")
        self.assertEqual(len(connection.sent), 1)

    def test_stable_server_error_is_not_retried(self):
        endpoint, connection = self.endpoint(packet(1, 2) + packet(2, 0, "ERR stale_owner search changed"))
        with self.assertRaises(gcmm.ReplyError) as caught:
            endpoint.command("gcmm_commit search_id=1 reservationid=1 match_id=2")
        self.assertEqual(caught.exception.code, "stale_owner")
        self.assertEqual(len(connection.sent), 2)

    def test_wrong_id_truncation_and_oversized_packets_fail_closed(self):
        cases = [packet(1, 2) + packet(9, 0, "OK {}"), b"\x01\x02", struct.pack("<i", 4097)]
        for data in cases:
            endpoint, _ = self.endpoint(data)
            with self.subTest(data=data), self.assertRaises(gcmm.GCMMError):
                endpoint.command("gcmm_status")

    def test_protocol_role_and_uint64_encoding_are_checked(self):
        for text in ["OK {}", "OK []", "OK not-json", "OK " + json.dumps(ok("server"))]:
            with self.subTest(text=text), self.assertRaises(gcmm.GCMMError):
                gcmm.decode_reply(text, "client")
        self.assertEqual(gcmm.identifier("1122334455667788", "cookie", hexadecimal=True), 0x1122334455667788)
        for value in [1, True, "-1", "18446744073709551616"]:
            with self.subTest(value=value), self.assertRaises(gcmm.GCMMError):
                gcmm.identifier(value, "search_id")

    def test_slow_fragmentation_cannot_reset_the_total_rpc_deadline(self):
        endpoint, connection = self.endpoint(packet(1, 2))
        endpoint.timeout = 0.25
        ticks = iter(index / 10 for index in range(20))
        with patch.object(gcmm.time, "monotonic", side_effect=lambda: next(ticks)):
            with self.assertRaisesRegex(gcmm.GCMMError, "transport failed"):
                endpoint.command("gcmm_status")
        self.assertEqual(len(connection.sent), 1)

    def test_cli_defaults_and_password_environment(self):
        args = gcmm.parser().parse_args(["--server", "127.0.0.1:37015", "--client", "127.0.0.1:37016",
                                         "--game-address", "203.0.113.121:27016", "--map", "de_dust2"])
        self.assertEqual(args.players, 10)
        self.assertFalse(args.once)
        with patch.dict(gcmm.os.environ, {"GCMM_TEST_SECRET": "test-password"}):
            endpoint = gcmm.endpoint_from_env("127.0.0.1:37016", "client", "GCMM_TEST_SECRET", 3)
            self.assertNotIn("test-password", repr(endpoint))
        with patch.dict(gcmm.os.environ, {}, clear=True), self.assertRaisesRegex(ValueError, "environment"):
            gcmm.endpoint_from_env("127.0.0.1:37016", "client", "GCMM_MISSING", 3)


if __name__ == "__main__":
    unittest.main()
