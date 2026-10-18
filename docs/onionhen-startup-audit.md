# onionHEN startup source audit

`tools/onionhen_startup_audit.py` is an offline, read-only check for
`aydencharles/onionHEN@aaa4264588e00da33faccc7642d27c3c4cc339b1` (`v0.9.0`).
It reads a local source checkout only; it neither downloads code nor executes a
payload.

Run it after checking out the pinned source:

```sh
python3 tools/onionhen_startup_audit.py /path/to/onionHEN
python3 -m unittest discover -s tests -p 'test_*.py'
```

The checker reports these expected static findings at the pinned revision:

- writes to `/system_ex/vsh_asset`, including `onionhen.png`;
- remount calls for both `/system_ex` and `/system`;
- `unlink` calls for `/update/PS5UPDATE.PUP` and
  `/update/PS5UPDATE.PUP.net.temp`;
- hardcoded system-partition icon paths used by bootstrapper, Settings, and
  HomeUI code.

The report is deliberately not a safety approval. Manual review remains needed
for other writes and first-run paths, any no-remount architecture, data-only
icon behavior, firmware 12.20 compatibility, loader/kstuff coexistence,
restart behavior, and rollback. It does not modify or test a console.
