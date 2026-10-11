# 90DNS Tester

A modified version of [Switch 90DNS Tester](https://github.com/meganukebmp/Switch_90DNS_tester), retaining the original application name. It displays legacy and FW 23 async resolver results side by side. The async additions are experimental local modifications, not an upstream release.

The async path is under investigation. A blocked result confirms the returned addresses for that query; it does not verify every application's traffic or guarantee protection from bans.

## Features

- Tests Nintendo domains using legacy `gethostbyname` and FW 23 Query + Fetch (sfdnsres commands 100/102).
- Shows separate Legacy and Async columns, with green blocked, red unblocked, and yellow inconclusive results.
- Checks every returned address: IPv4 `127.0.0.1`/`0.0.0.0` and async IPv6 `::1`/`::` count as blocked.
- Shows an end-of-run summary; unknown results are never counted as blocked.
- On older firmware, Async displays N/A.
- Appends results and IPC diagnostics to `sdmc:/90dns-test-log.txt`.

## Using 90DNS Tester

Extract the ZIP to the SD card and launch `/switch/Switch_90DNS_tester.nro` from the Homebrew Menu. Tests start automatically. Press X to retry both tests and B to exit after the run.

| Status | Meaning |
| --- | --- |
| blocked | All returned addresses match the blocking addresses above. |
| unblocked | At least one returned address is outside those addresses. |
| event-wait | No completion event within 2 seconds; Fetch was not attempted. Blocking is unverified. |
| timeout | The resolver reported a DNS timeout. |
| unresolved | The resolver did not return a successful address result. |
| offline | No current network IP was available. |
| error | IPC, event handling, or result parsing failed. |
| N/A | Async requires firmware 23.0.0 or newer. |

The log records console time, firmware, EventFd, Query, Poll, EventRead, Fetch when ready, and Cancel. The async event wait is limited to 2 seconds per domain; this is not a total runtime limit. Legacy DNS calls and other IPC calls may take additional time. The latest tested predecessor showed several async results followed by event timeouts; the cause remains unconfirmed.

## Building

Requires [devkitPro](https://devkitpro.org/) with libnx installed.

```bash
make
```

`make` builds `Switch_90DNS_tester.nro` and creates `Switch_90DNS_tester-1.0.4.zip` containing `switch/Switch_90DNS_tester.nro`. The name stays fixed when Docker mounts the project at `/work`. Use the existing devkitPro PowerShell/Docker wrapper or a configured devkitPro shell.

## Credits

Based on [Switch 90DNS Tester](https://github.com/meganukebmp/Switch_90DNS_tester), originally authored by Nexrem and maintained by its contributors. Local modifications are maintained by STHETIX. The application icon is inherited from the original project.

The FW 23 IPC layout and serialization were checked against [Atmosphere's DNS MITM implementation](https://github.com/Atmosphere-NX/Atmosphere/commit/f5cd912604c342faf884e776b1aaaa5f9b3b5e98).

## License

LGPL-3.0. See [COPYING](COPYING).
