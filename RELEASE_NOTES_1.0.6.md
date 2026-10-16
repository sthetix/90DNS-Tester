# 90DNS Tester v1.0.6

This version adds a standalone graphical interface with a HATS-style dark theme and mint accents, while keeping the original 90DNS Tester name.

- View all 35 domain entries on one screen in two side-by-side tables, without scrolling.
- Compare Legacy and c-ares DNS results, with separate blocked, unblocked, and unverified counts.
- Follow test progress while the interface remains responsive.
- Retry with X or the touch button; exit with B, +, or the touch button. Exiting during a test waits for the current resolver call to return.
- See the application version and firmware information below the title.
- Save results and resolver diagnostics to `sdmc:/90dns-test-log.txt`, with clearer timeout diagnostics and log write status.

The c-ares test uses the newer DNS resolver commands introduced in firmware 23.0.0. Older firmware displays N/A for that test. Timeout, unresolved, offline, and error results remain unverified and are never counted as blocked.

Blocked results describe the DNS queries tested; they do not verify every application's traffic or guarantee protection from bans.

Based on the original Switch 90DNS Tester by Nexrem and its contributors. The NanoVG/deko3d renderer comes from the Daybreak bundle, with its original license notices retained. This application does not include firmware installation functionality.
