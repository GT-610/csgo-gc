# GC Matchmaking (GCMM) Protocol reference coordinator

This directory contains the Python 3.10+ standard-library coordinator for the
`gcmm/1` Source RCON native-control profile. It uses the stable `gcmm_*` commands;
it does not read probe logs or require Bun, PowerShell scripts, Npcap, a debugger,
or process-memory access.

The coordinator gathers actual searching client identities, reserves the native
server, prepares all clients concurrently, waits for their native ACCEPT clicks,
and commits only after the server reports the exact owned roster at stage 2 or
higher. The native client handles the connection. The coordinator neither clicks
ACCEPT nor sends a console connection command.

## Scope and prerequisites

- A configured dedicated server on the supported Windows x86 legacy 1575 native
  adapter. Unsupported binaries fail discovery before reservation.
- Each client and server has GCMM enabled independently of RCON:

  ```text
  "gcmm"
  {
      "enabled" "1"
  }
  ```

  RCON must also be enabled with its own bind address, port, and password. Supply
  the GCMM RCON ports here, which are separate from the game server's UDP port.
  `mm_probe` is optional diagnostic logging and can remain disabled.
- The dedicated server already runs the map/mode supplied to the coordinator.
  This example does not configure maps or allocate dedicated servers.
- The default cohort is **ten distinct real accounts** on ten reachable client
  RCON endpoints. Each person starts a competitive solo-party search in their
  own game. A party request with multiple accounts produces an explicit error;
  this first reference implementation does not split or assign parties.
- The default offered game type is `4104`. Requested and offered game types must
  have low nibble `8` (competitive). Their complete values need not be equal: a
  real 1575 request can contain `462552584`.
- Passwords come from environment variables. Source RCON provides authentication
  without transport encryption; expose it only on the trusted network used by
  the coordinator.

## Explicit one-real-account smoke run

Set the two secrets in the coordinator's environment. In PowerShell 7, for example:

```powershell
$env:GCMM_SERVER_PASSWORD = Read-Host 'Server RCON password' -MaskInput
$env:GCMM_CLIENT_PASSWORD = Read-Host 'Client RCON password' -MaskInput
python examples/gcmm/coordinator.py --server 127.0.0.1:37015 --client 127.0.0.1:37016 --game-address 203.0.113.121:27016 --map de_dust2 --players 1 --once
```

Replace the example addresses with your configured endpoints. The game address
must be reachable from every participant. A loopback RCON address is appropriate
only when that endpoint and the coordinator run on the same machine.

Then start the search in the game, click the native ACCEPT button when it appears,
and let the client complete its native connection. The process exits successfully
after all commits and client committed-state verification. `--once` does not wait
for the game to finish and does not release a successful reservation while clients
still need it for connection.

A one-account smoke run validates that account's flow. It does not establish that
ten real accounts have passed an acceptance test.

## Ten real participants

Repeat `--client` for each real participant's RCON endpoint. Ten is the default;
there is no account-id list to fill in and no generated placeholder participant.
For example, after replacing these endpoint hostnames with your own:

```powershell
python examples/gcmm/coordinator.py --server server-host:37015 --client client01:37016 --client client02:37016 --client client03:37016 --client client04:37016 --client client05:37016 --client client06:37016 --client client07:37016 --client client08:37016 --client client09:37016 --client client10:37016 --game-address 203.0.113.121:27016 --map de_dust2 --once
```

One `--client-password-env NAME` applies that environment variable to every client.
To use distinct passwords, repeat this option once per `--client` in matching
order. If omitted, all clients use `GCMM_CLIENT_PASSWORD`. The server variable can
be changed with `--server-password-env NAME`, default `GCMM_SERVER_PASSWORD`.
Credentials are absent from command arguments, configuration files, and logs.

Without `--once`, the process waits for the configured server to become available
and for the next full cohort of real searches after a successful match. The
reference process manages one configured server; it does not implement the full
match lifecycle, ranked matchmaking, federation, or server provisioning.

## Controls and timing

Run `python examples/gcmm/coordinator.py --help` for the full CLI.

| Option | Default | Meaning |
|---|---:|---|
| `--players` | 10 | Distinct real searching accounts required; `1` is an explicit smoke profile. |
| `--rpc-timeout` | 4 seconds | Total deadline for one authenticated RCON operation; timed-out mutations are not replayed automatically. |
| `--prepare-timeout` | 12 seconds | Native activation deadline and limit on the concurrent client-prepare phase. |
| `--accept-timeout` | 90 seconds | Coordinator's upper bound for real clicks; the native server may expire earlier. |
| `--search-timeout` | 0 | Time to gather the cohort; 0 waits indefinitely. |
| `--poll-interval` | 0.25 seconds | Status polling delay. |
| `--server-version` | 13881 | Expected native client protocol version. |
| `--game-type` | 4104 | Offered competitive game type. |

Client preparation uses independent authenticated RCON connections in a thread
pool. It does not perform ten slow reserves sequentially inside the native
reservation's approximately 21-second initial window. All attempted operations
finish before failure cleanup starts, preventing a late successful prepare from
arriving after cleanup. If the native reservation expires or its owner changes,
the coordinator aborts and never treats elapsed time as acceptance.

Press Ctrl+C to stop. Exit codes: `0` for a completed `--once` commit, `1` for an
error, `2` for an argparse usage error, and `130` for operator interruption.

## Stable API used

1. `gcmm_status` on server and clients: validate protocol/role, capability,
   actual identities, native search ids, mode, and solo-party requests.
2. Server `gcmm_reserve reservationid=... match_id=... accounts=...`: obtain
   `request_id`, then poll the exact server owner and real roster until active.
3. Concurrent client `gcmm_reserve search_id=... reservationid=... match_id=...
   accounts=... ip=... port=... serverid=... map=... game_type=...
   server_address=... server_version=...` with each client's own search id.
4. Poll client ownership and server status. Require a successful JSON boolean
   `last_result`, matching request/cookie/match/roster, active reservation,
   `all_accepted=true`, and **every actual roster entry's stage >= 2**. Revalidate
   clients and server immediately before publication.
5. Concurrent client `gcmm_commit search_id=... reservationid=... match_id=...`,
   then verify every client's committed state.
6. On cancellation, error, timeout, or interruption, cancel only attempted client
   search owners with `gcmm_cancel search_id=...` and release only this native
   cookie/match owner with `gcmm_release reservationid=... match_id=...`. A new
   search or another server reservation is preserved. Release completion requires
   the matching release request id, a successful native result, and an inactive
   reservation; the engine may clear the old cookie and queue on a later pass.
   Cleanup is best effort if an endpoint becomes unreachable; its exact
   owner-checked commands remain safe, and incomplete cleanup is reported.

All identifiers follow the GCMM contract: uint64 values in JSON are strings,
search/request ids are decimal strings, cookies/match ids are hexadecimal strings,
and account ids are JSON uint32 numbers. No caller-supplied `stage=2` claim is
accepted as readiness evidence.

## Offline tests

```text
python -m unittest discover -s examples/gcmm -p test_coordinator.py -v
```

The tests use simulated endpoints and fragmented in-memory Source RCON packets.
They open no network sockets. They cover concurrent ten-client preparation,
waiting for the last native ACCEPT even if an aggregate boolean is wrong,
no fabricated or duplicate accounts, unsupported parties/modes, cancellation,
stale owners, lost replies, partial preparation, timeouts, interruption, framing,
authentication, and password handling.

Multi-client simulation verifies the coordinator's rules. Ten real clients and
production distributed matchmaking still require separate runtime validation.
