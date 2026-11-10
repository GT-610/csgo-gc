#!/usr/bin/env python3
"""GCMM/1 Source RCON reference coordinator (Python 3.10+, standard library).

Only real searching clients supplied by the operator participate. The native
client owns ACCEPT; only the authoritative server's actual stage-2 roster can
allow gcmm_commit. See README.md for the intentionally limited solo-party profile.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
import ipaddress
import json
import logging
import math
import os
import re
import secrets
import socket
import struct
import sys
import threading
import time
from typing import Callable, Protocol

PROTOCOL = "gcmm/1"
MAX_PACKET = 4096
UINT64_MAX = (1 << 64) - 1
LOG = logging.getLogger("gcmm")


class GCMMError(RuntimeError):
    """A failed transaction; cleanup must not replace the original error."""


class ReplyError(GCMMError):
    def __init__(self, code: str, message: str):
        self.code = code
        super().__init__(f"{code}: {message}")


class StaleOwner(GCMMError):
    pass


class Endpoint(Protocol):
    label: str
    role: str

    def command(self, command: str) -> dict:
        ...


def split_address(value: str) -> tuple[str, int]:
    """Parse a Source RCON endpoint; bracketed IPv6 is also accepted."""
    if value.startswith("["):
        match = re.fullmatch(r"\[([^\]]+)\]:(\d+)", value)
        if not match:
            raise ValueError("expected [IPv6]:port")
        host, port_text = match.groups()
    else:
        host, separator, port_text = value.rpartition(":")
        if not separator or not host or ":" in host or any(c.isspace() for c in host):
            raise ValueError("expected host:port (IPv6 must use brackets)")
    if not port_text.isascii() or not port_text.isdecimal():
        raise ValueError("port must be a decimal integer")
    port = int(port_text)
    if not 1 <= port <= 65535:
        raise ValueError("port must be between 1 and 65535")
    return host, port


def identifier(value: object, field_name: str, *, hexadecimal: bool = False,
               allow_zero: bool = False) -> int:
    if not isinstance(value, str):
        raise GCMMError(f"{field_name} must be a JSON string")
    pattern = r"(?:0[xX])?[0-9a-fA-F]+" if hexadecimal else r"[0-9]+"
    if not re.fullmatch(pattern, value):
        raise GCMMError(f"invalid {field_name}")
    number = int(value, 16 if hexadecimal else 10)
    if not (0 if allow_zero else 1) <= number <= UINT64_MAX:
        raise GCMMError(f"{field_name} outside uint64 range")
    return number


def account_id(value: object) -> int:
    if type(value) is not int or not 1 <= value <= 0xFFFFFFFF:
        raise GCMMError("account id must be a nonzero uint32 JSON number")
    return value


def decode_reply(text: str, role: str) -> dict:
    if text.startswith("ERR "):
        code, _, message = text[4:].partition(" ")
        raise ReplyError(code or "invalid_error", message)
    if not text.startswith("OK "):
        raise GCMMError("expected GCMM OK JSON or ERR code response")
    try:
        response = json.loads(text[3:])
    except (ValueError, TypeError) as error:
        raise GCMMError("invalid GCMM JSON response") from error
    if not isinstance(response, dict) or response.get("protocol") != PROTOCOL:
        raise GCMMError("endpoint does not speak gcmm/1")
    if response.get("role") != role:
        raise GCMMError(f"expected GCMM {role} endpoint")
    return response


@dataclass
class SourceRconEndpoint:
    address: tuple[str, int]
    role: str
    password: str = field(repr=False)
    timeout: float = 4.0
    socket_factory: Callable = field(default=socket.create_connection, repr=False)
    _lock: threading.Lock = field(default_factory=threading.Lock, init=False, repr=False)
    _next_id: int = field(default=1, init=False, repr=False)

    @property
    def label(self) -> str:
        return f"{self.role}@{self.address[0]}:{self.address[1]}"

    @staticmethod
    def _remaining(connection, deadline: float) -> None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("RCON operation exceeded its deadline")
        connection.settimeout(remaining)

    @classmethod
    def _read_exact(cls, connection, length: int, deadline: float) -> bytes:
        result = bytearray()
        while len(result) < length:
            cls._remaining(connection, deadline)
            block = connection.recv(length - len(result))
            if not block:
                raise GCMMError("RCON connection closed before a full packet arrived")
            result.extend(block)
        return bytes(result)

    @classmethod
    def _receive(cls, connection, deadline: float) -> tuple[int, int, str]:
        length = struct.unpack("<i", cls._read_exact(connection, 4, deadline))[0]
        if not 10 <= length <= MAX_PACKET:
            raise GCMMError("RCON packet size outside 10..4096 bytes")
        packet = cls._read_exact(connection, length, deadline)
        if not packet.endswith(b"\0\0"):
            raise GCMMError("RCON packet lacks the two terminators")
        request_id, kind = struct.unpack("<ii", packet[:8])
        try:
            body = packet[8:-2].decode("utf-8")
        except UnicodeDecodeError as error:
            raise GCMMError("RCON response is not UTF-8") from error
        return request_id, kind, body

    @staticmethod
    def _send(connection, request_id: int, kind: int, body: str) -> None:
        encoded = body.encode("utf-8")
        if b"\0" in encoded or len(encoded) + 10 > MAX_PACKET:
            raise GCMMError("invalid or oversized RCON request")
        packet = struct.pack("<ii", request_id, kind) + encoded + b"\0\0"
        connection.sendall(struct.pack("<i", len(packet)) + packet)

    def command(self, command: str) -> dict:
        # Independent connections bound a failed operation and avoid replaying a
        # mutation after a transport timeout. Each endpoint is serialized locally.
        with self._lock:
            auth_id = self._next_id
            command_id = auth_id + 1
            self._next_id = 1 if command_id >= 0x7FFFFFFD else command_id + 1
            deadline = time.monotonic() + self.timeout
            try:
                with self.socket_factory(self.address, self.timeout) as connection:
                    self._remaining(connection, deadline)
                    self._send(connection, auth_id, 3, self.password)
                    for _ in range(3):
                        received_id, kind, _ = self._receive(connection, deadline)
                        if received_id == -1:
                            raise ReplyError("authentication_failed", self.label)
                        if received_id != auth_id:
                            raise GCMMError("RCON authentication request id mismatch")
                        if kind == 2:
                            break
                        if kind != 0:
                            raise GCMMError("unexpected RCON authentication packet")
                    else:
                        raise GCMMError("RCON authentication response missing")
                    self._remaining(connection, deadline)
                    self._send(connection, command_id, 2, command)
                    received_id, kind, body = self._receive(connection, deadline)
                    if received_id != command_id or kind != 0:
                        raise GCMMError("RCON command response id/type mismatch")
                    return decode_reply(body, self.role)
            except (OSError, TimeoutError) as error:
                raise GCMMError(f"{self.label}: RCON transport failed ({type(error).__name__})") from error


@dataclass(frozen=True)
class Candidate:
    endpoint: Endpoint
    account: int
    search: int


@dataclass
class Transaction:
    clients: list[Candidate]
    cookie: int
    match: int
    request: int | None = None
    attempted: list[Candidate] = field(default_factory=list)
    native_requested: bool = False

    @property
    def owner_arguments(self) -> str:
        return f"reservationid=0x{self.cookie:016x} match_id=0x{self.match:016x}"


class Coordinator:
    def __init__(self, server: Endpoint, clients: list[Endpoint], *,
                 game_address: tuple[str, int], map_name: str, players: int = 10,
                 game_type: int = 4104, server_version: int = 13881,
                 poll_interval: float = 0.25, prepare_timeout: float = 12.0,
                 accept_timeout: float = 90.0, clock: Callable = time.monotonic,
                 sleep: Callable = time.sleep, id_factory: Callable = lambda: secrets.randbits(64)):
        if not 1 <= players <= 64 or len(clients) < players:
            raise ValueError("provide at least --players distinct client endpoints (1..64)")
        if len({endpoint.label for endpoint in clients}) != len(clients):
            raise ValueError("duplicate client RCON endpoint")
        if type(game_type) is not int or not 0 <= game_type <= 0xFFFFFFFF or game_type & 15 != 8:
            raise ValueError("GCMM native ACCEPT profile requires competitive game_type (low nibble 8)")
        ip = ipaddress.IPv4Address(game_address[0])
        if ip.is_unspecified or ip.is_multicast or int(ip) >> 24 == 0 or int(ip) >> 24 >= 224:
            raise ValueError("game address must be a unicast IPv4 address")
        if not 1 <= game_address[1] <= 65535:
            raise ValueError("invalid game port")
        if not re.fullmatch(r"[A-Za-z0-9_/-]{1,128}", map_name):
            raise ValueError("map must be 1..128 letters, digits, underscores, slashes or hyphens")
        if any(not math.isfinite(value) or value <= 0
               for value in (poll_interval, prepare_timeout, accept_timeout)):
            raise ValueError("poll and timeout values must be finite and positive")
        if type(server_version) is not int or not 1 <= server_version <= 0xFFFFFFFF:
            raise ValueError("server version must be a nonzero uint32")
        self.server, self.clients = server, clients
        self.game_address, self.map_name, self.players = (str(ip), game_address[1]), map_name, players
        self.game_type, self.server_version = game_type, server_version
        self.poll_interval, self.prepare_timeout, self.accept_timeout = poll_interval, prepare_timeout, accept_timeout
        self.clock, self.sleep, self.id_factory = clock, sleep, id_factory

    @staticmethod
    def _parallel(items: list, action: Callable) -> list:
        # All operations finish before propagating an error, so a late successful
        # prepare cannot arrive after transaction cleanup has already completed.
        with ThreadPoolExecutor(max_workers=len(items)) as pool:
            futures = [pool.submit(action, item) for item in items]
            results, failures = [], []
            for future in futures:
                try:
                    results.append(future.result())
                except Exception as error:
                    failures.append(error)
            if failures:
                raise failures[0]
            return results

    @staticmethod
    def _status(endpoint: Endpoint) -> dict:
        result = endpoint.command("gcmm_status")
        if result.get("protocol") != PROTOCOL or result.get("role") != endpoint.role:
            raise GCMMError(f"{endpoint.label}: invalid protocol/role")
        if result.get("enabled") is not True:
            raise GCMMError(f"{endpoint.label}: enable gcmm and RCON independently")
        if endpoint.role == "server" and result.get("initialized") is not True:
            raise GCMMError(f"{endpoint.label}: native adapter unavailable: {result.get('error', 'unsupported build')}")
        return result

    def discover(self) -> list[Candidate]:
        server = self._status(self.server)
        if server.get("active") is True:
            return []
        statuses = self._parallel(self.clients, self._status)
        result, seen = [], set()
        for endpoint, status in zip(self.clients, statuses):
            if status.get("phase") != "searching":
                continue
            account = account_id(status.get("account_id"))
            steam = identifier(status.get("steam_id"), "steam_id")
            if steam & 0xFFFFFFFF != account:
                raise GCMMError(f"{endpoint.label}: Steam/account identity mismatch")
            search = identifier(status.get("search_id"), "search_id")
            request = status.get("request")
            if not isinstance(request, dict) or request.get("account_ids") != [account]:
                raise GCMMError(f"{endpoint.label}: solo-party reference supports exactly one local account per request; split the party first")
            requested_type = request.get("game_type")
            if type(requested_type) is not int or requested_type & 15 != 8:
                raise GCMMError(f"{endpoint.label}: request competitive matchmaking for native ACCEPT")
            if request.get("client_version") != self.server_version:
                raise GCMMError(f"{endpoint.label}: client version differs from --server-version")
            if account in seen:
                raise GCMMError("multiple client endpoints report the same real account")
            seen.add(account)
            result.append(Candidate(endpoint, account, search))
        return result[:self.players] if len(result) >= self.players else []

    def _server_owner(self, tx: Transaction, status: dict) -> bool:
        request = identifier(status.get("request_id"), "request_id", allow_zero=True)
        cookie = identifier(status.get("cookie"), "cookie", hexadecimal=True, allow_zero=True)
        match = identifier(status.get("match_id"), "match_id", hexadecimal=True, allow_zero=True)
        if tx.request is not None and request != tx.request:
            if request > tx.request or status.get("active") is True:
                raise StaleOwner("server request owner changed")
            return False
        if cookie != tx.cookie or match != tx.match:
            if status.get("active") is True:
                raise StaleOwner("server reservation owner changed")
            return False
        roster = status.get("roster")
        if not isinstance(roster, list):
            raise GCMMError("server roster is missing")
        actual = [account_id(row.get("account_id")) for row in roster if isinstance(row, dict)]
        expected = [client.account for client in tx.clients]
        if len(actual) != len(roster) or len(actual) != len(expected) or set(actual) != set(expected):
            raise GCMMError("server real roster differs from the selected real clients")
        if status.get("roster_count") != len(expected) or status.get("expected_humans") != len(expected):
            raise GCMMError("server roster counters differ from the selected clients")
        if type(status.get("last_result")) is not bool:
            raise GCMMError("server last_result must be a JSON boolean")
        return (status["last_result"] is True and status.get("matches_request") is True
                and status.get("active") is True)

    @staticmethod
    def _same_reservation(tx: Transaction, status: dict) -> bool:
        reserve = status.get("reservation")
        return isinstance(reserve, dict) and (
            identifier(reserve.get("reservationid"), "reservationid", hexadecimal=True) == tx.cookie
            and identifier(reserve.get("match_id"), "match_id", hexadecimal=True) == tx.match)

    def _check_clients(self, tx: Transaction, phase: str = "reserved") -> None:
        statuses = self._parallel(tx.clients, lambda client: self._status(client.endpoint))
        expected = {client.account for client in tx.clients}
        for client, status in zip(tx.clients, statuses):
            if identifier(status.get("search_id"), "search_id", allow_zero=True) != client.search:
                raise StaleOwner(f"{client.endpoint.label}: search owner changed")
            if status.get("phase") != phase or not self._same_reservation(tx, status):
                raise StaleOwner(f"{client.endpoint.label}: search cancelled or reservation changed")
            accounts = status["reservation"].get("accounts")
            if not isinstance(accounts, list):
                raise GCMMError(f"{client.endpoint.label}: held roster is missing")
            accounts = [account_id(value) for value in accounts]
            if len(accounts) != len(expected) or set(accounts) != expected:
                raise GCMMError(f"{client.endpoint.label}: held roster differs from server transaction")

    def _fresh_id(self) -> int:
        for _ in range(32):
            result = self.id_factory()
            if type(result) is int and 1 <= result <= UINT64_MAX:
                return result
        raise GCMMError("could not generate a nonzero uint64 transaction id")

    def run_match(self, chosen: list[Candidate]) -> Transaction:
        if len(chosen) != self.players or len({c.account for c in chosen}) != self.players:
            raise GCMMError("transaction requires exactly players distinct real accounts")
        tx = Transaction(chosen, self._fresh_id(), self._fresh_id())
        roster = ",".join(str(client.account) for client in chosen)
        LOG.info("Reserving server for %d real account(s), match=0x%016x", len(chosen), tx.match)
        try:
            tx.native_requested = True
            response = self.server.command(f"gcmm_reserve {tx.owner_arguments} accounts={roster}")
            tx.request = identifier(response.get("request_id"), "request_id")
            deadline = self.clock() + self.prepare_timeout
            while True:
                status = self._status(self.server)
                if self._server_owner(tx, status):
                    break
                if self.clock() >= deadline:
                    raise GCMMError("native server reservation did not become active before prepare timeout")
                self.sleep(self.poll_interval)
            server_id = identifier(status.get("server_steamid"), "server_steamid")
            ip, port = self.game_address
            common = (f"{tx.owner_arguments} accounts={roster} ip={ip} port={port} "
                      f"serverid={server_id} map={self.map_name} game_type={self.game_type} "
                      f"server_address={ip}:{port} server_version={self.server_version}")
            tx.attempted = list(chosen)
            started = self.clock()
            self._parallel(chosen, lambda client: client.endpoint.command(
                f"gcmm_reserve search_id={client.search} {common}"))
            if self.clock() - started >= self.prepare_timeout:
                raise GCMMError("client prepare exceeded deadline; refusing a potentially expired native reservation")
            self._check_clients(tx)
            LOG.info("All %d clients reserved; waiting for their native ACCEPT clicks", len(chosen))
            deadline = self.clock() + self.accept_timeout
            previous = None
            while True:
                self._check_clients(tx)
                status = self._status(self.server)
                if not self._server_owner(tx, status):
                    raise StaleOwner("native server reservation is no longer active")
                stages = [row.get("stage") for row in status["roster"]]
                if any(type(stage) is not int or not 0 <= stage <= 0xFFFFFFFF for stage in stages):
                    raise GCMMError("server reported an invalid native stage")
                accepted = sum(stage >= 2 for stage in stages)
                if accepted != previous:
                    LOG.info("Native ACCEPT: %d/%d", accepted, len(chosen))
                    previous = accepted
                if accepted == len(chosen) and status.get("all_accepted") is True:
                    # Revalidate clients immediately before publication. gcmm_commit
                    # also rejects a stale search locally if cancellation races this.
                    self._check_clients(tx)
                    final = self._status(self.server)
                    if not self._server_owner(tx, final):
                        raise StaleOwner("server owner changed immediately before commit")
                    final_stages = [row.get("stage") for row in final["roster"]]
                    if (final.get("all_accepted") is not True
                            or any(type(stage) is not int or not 2 <= stage <= 0xFFFFFFFF
                                   for stage in final_stages)):
                        raise GCMMError("server readiness changed immediately before commit")
                    break
                if self.clock() >= deadline:
                    raise GCMMError("timed out waiting for real native ACCEPT; transaction aborted")
                self.sleep(self.poll_interval)
            self._parallel(chosen, lambda client: client.endpoint.command(
                f"gcmm_commit search_id={client.search} {tx.owner_arguments}"))
            self._check_clients(tx, "committed")
            LOG.info("Committed match=0x%016x for %d real client(s); native clients now own connection", tx.match, len(chosen))
            return tx
        except BaseException:
            self.cleanup(tx)
            raise

    def cleanup(self, tx: Transaction) -> None:
        LOG.warning("Aborting owned match=0x%016x; cancelling prepared searches and releasing its server owner", tx.match)

        def cancel_client(client: Candidate) -> None:
            try:
                try:
                    status = self._status(client.endpoint)
                except Exception as error:
                    # The API itself rejects a stale search; an unavailable read
                    # must not prevent an owner-guarded cancellation attempt.
                    LOG.warning("Cannot read %s during cleanup: %s", client.endpoint.label, error)
                    status = None
                if status is not None:
                    if identifier(status.get("search_id"), "search_id", allow_zero=True) != client.search:
                        LOG.warning("Skip stale client owner: %s", client.endpoint.label)
                        return
                    if status.get("phase") in ("reserved", "committed") and not self._same_reservation(tx, status):
                        LOG.warning("Skip different client reservation: %s", client.endpoint.label)
                        return
                    if status.get("phase") == "idle":
                        return
                client.endpoint.command(f"gcmm_cancel search_id={client.search}")
            except Exception as error:
                LOG.warning("Client cleanup incomplete for %s: %s", client.endpoint.label, error)

        if tx.attempted:
            self._parallel(tx.attempted, cancel_client)
        if tx.native_requested:
            try:
                try:
                    status = self._status(self.server)
                except Exception as error:
                    LOG.warning("Cannot read server during cleanup: %s", error)
                    status = None
                if status is not None:
                    cookie = identifier(status.get("cookie"), "cookie", hexadecimal=True, allow_zero=True)
                    match = identifier(status.get("match_id"), "match_id", hexadecimal=True, allow_zero=True)
                    if (cookie, match) != (tx.cookie, tx.match):
                        LOG.warning("Skip release: server no longer holds this transaction")
                        return
                # Even if the status read failed, these exact identifiers keep the
                # mutation owner-checked on the server. Never use an unowned clear.
                release_response = self.server.command(f"gcmm_release {tx.owner_arguments}")
                release_request = identifier(release_response.get("request_id"), "request_id")
                deadline = self.clock() + self.prepare_timeout
                while True:
                    status = self._status(self.server)
                    cookie = identifier(status.get("cookie"), "cookie", hexadecimal=True, allow_zero=True)
                    match = identifier(status.get("match_id"), "match_id", hexadecimal=True, allow_zero=True)
                    if (cookie, match) != (tx.cookie, tx.match):
                        return
                    # The native API expires an owned reservation immediately;
                    # the engine may clear its cookie/queue on a later pass.
                    if (identifier(status.get("request_id"), "request_id", allow_zero=True) == release_request
                            and status.get("last_result") is True and status.get("active") is False):
                        return
                    if self.clock() >= deadline:
                        raise GCMMError("native release did not complete before cleanup timeout")
                    self.sleep(self.poll_interval)
            except Exception as error:
                LOG.warning("Server cleanup incomplete: %s", error)

    def run_once(self, search_timeout: float = 0.0) -> Transaction:
        deadline = self.clock() + search_timeout if search_timeout else None
        LOG.info("Waiting for %d distinct real solo-party searches", self.players)
        while True:
            candidates = self.discover()
            if candidates:
                return self.run_match(candidates)
            if deadline is not None and self.clock() >= deadline:
                raise GCMMError("timed out waiting for the required real searching clients")
            self.sleep(self.poll_interval)


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    result.add_argument("--server", required=True, help="server Source RCON host:port")
    result.add_argument("--server-password-env", default="GCMM_SERVER_PASSWORD")
    result.add_argument("--client", action="append", required=True, help="repeat for every real client's Source RCON host:port")
    result.add_argument("--client-password-env", action="append", help="one env name for all clients, or repeat once per --client; default GCMM_CLIENT_PASSWORD")
    result.add_argument("--game-address", required=True, help="native game server IPv4:port, reachable by every client")
    result.add_argument("--map", dest="map_name", required=True, help="map already configured on the dedicated server")
    result.add_argument("--players", type=int, default=10, help="real participants required (default 10); use 1 explicitly for smoke validation")
    result.add_argument("--game-type", type=int, default=4104)
    result.add_argument("--server-version", type=int, default=13881)
    result.add_argument("--rpc-timeout", type=float, default=4.0)
    result.add_argument("--prepare-timeout", type=float, default=12.0)
    result.add_argument("--accept-timeout", type=float, default=90.0)
    result.add_argument("--search-timeout", type=float, default=0.0, help="0 waits indefinitely for searches")
    result.add_argument("--poll-interval", type=float, default=0.25)
    result.add_argument("--once", action="store_true", help="coordinate one match, then exit after successful commit")
    return result


def endpoint_from_env(address: str, role: str, variable: str, timeout: float) -> SourceRconEndpoint:
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", variable):
        raise ValueError("password environment variable name is invalid")
    password = os.environ.get(variable)
    if not password:
        raise ValueError(f"set nonempty password environment variable {variable}")
    return SourceRconEndpoint(split_address(address), role, password, timeout)


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    try:
        if (not math.isfinite(arguments.rpc_timeout) or arguments.rpc_timeout <= 0
                or not math.isfinite(arguments.search_timeout) or arguments.search_timeout < 0):
            raise ValueError("RPC timeout must be finite/positive and search timeout finite/nonnegative")
        variables = arguments.client_password_env or ["GCMM_CLIENT_PASSWORD"]
        if len(variables) == 1:
            variables *= len(arguments.client)
        if len(variables) != len(arguments.client):
            raise ValueError("provide one --client-password-env, or one per --client in the same order")
        server = endpoint_from_env(arguments.server, "server", arguments.server_password_env, arguments.rpc_timeout)
        clients = [endpoint_from_env(address, "client", variable, arguments.rpc_timeout)
                   for address, variable in zip(arguments.client, variables)]
        coordinator = Coordinator(server, clients, game_address=split_address(arguments.game_address),
                                  map_name=arguments.map_name, players=arguments.players,
                                  game_type=arguments.game_type, server_version=arguments.server_version,
                                  poll_interval=arguments.poll_interval, prepare_timeout=arguments.prepare_timeout,
                                  accept_timeout=arguments.accept_timeout)
        if arguments.players == 1:
            LOG.warning("Explicit one-real-account smoke profile; this does not validate ten real clients")
        while True:
            coordinator.run_once(arguments.search_timeout)
            if arguments.once:
                return 0
            LOG.info("Waiting for server availability and the next real search cohort")
    except KeyboardInterrupt:
        LOG.warning("Stopped by operator; any active transaction has run owner-checked cleanup")
        return 130
    except (GCMMError, ValueError) as error:
        LOG.error("%s", error)
        return 1


if __name__ == "__main__":
    sys.exit(main())
