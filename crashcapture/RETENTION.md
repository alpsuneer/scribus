# Retention policy for ~/scribus-crashlogs

Run daily by the `scribus-housekeeping` systemd **user** timer as `s1`
(`loginctl enable-linger s1` is set, so it runs whether or not you are logged
in). The tool is `housekeeping.sh`; it deletes nothing unless given `--apply`.

    housekeeping.sh            dry run — print what would go
    housekeeping.sh --report   full keep/delete classification, every file
    housekeeping.sh --apply    act, and append to housekeeping.log

    systemctl --user list-timers scribus-housekeeping.timer
    systemctl --user start scribus-housekeeping.service    # run it now

## What gets deleted

Deletion is **opt-in by pattern**. A file is removed only if it positively
matches a throwaway class; anything unrecognised is kept.

| Class     | What                                                        |
|-----------|-------------------------------------------------------------|
| `run`     | whole `runs/<YYYY-MM-DD>/` folders, aged by the folder name  |
| `core`    | `core-*` (compressed or not)                                 |
| `log`     | `session-*.log`                                              |
| `testout` | `tabletest/**` `.png` `.eps` `.ps` `.pdf` `.log` `.txt`      |
| `testdir` | `tabletest/*home*/` and `tabletest/pd_*/` — every one of     |
|           | these is `rm -rf`'d and rebuilt by its own battery script    |
| `battery` | `battery-out/*.txt`                                          |

Two limits, in this order:

1. **Age** — throwaway older than 7 days goes.
2. **Size** — if the folder still exceeds 2 GB of *real disk* (not apparent
   size; cores are sparse), keep taking throwaway oldest-first, **cores first**,
   until it fits. The cap is allowed to be missed rather than touch a protected
   file.

## What is never deleted

- `.sh` `.py` `.patch` `.md` `.sla` `.xml` anywhere
- **`crash-*.log` anywhere** — 30 KB each and the actual diagnostic evidence.
  The cores they pair with are the bulk and those do expire.
- any file starting with `#!`; the stub `lpr`/`lpoptions` harness
- everything under `repro/`
- `housekeeping.sh` and its log
- **any directory containing a `.keep` file** — drop one in to pin a run folder
  or anything else indefinitely.

## The runs/ convention

Throwaway output goes in a dated folder so retention is a matter of dropping
whole old folders instead of pattern-matching filenames.

- `scribus-debug` → `runs/<date>/session-<ts>.log`, `runs/<date>/core-<ts>`.
  The crash backtrace still goes to the **top level** as `crash-<ts>.log`,
  because it is kept forever and must not sit in an expiring folder.
- `run_batteries.sh` exports `SCRIBUS_TEST_OUT=runs/<date>/battery`. Each
  battery uses `OUT="${SCRIBUS_TEST_OUT:-$DIR}"`, so running one by hand still
  writes to `tabletest/` exactly as before.
- Inputs stay in `tabletest/` — `fakebin/`, `carettest/caret_src.sla`,
  `psbench/mask_src.sla`, `psbench/bench_40.sla`. Do not move them into a run
  folder; the batteries read them by fixed path.

Anything inside `runs/` is deletable by construction, *except* a `crash-*.log`
or a folder holding a `.keep`.

## Gotchas

- The job runs as `s1`. Unlinking needs write permission on the **directory**,
  so a root-owned subtree makes deletion fail; the tool reports `[N FAILED]`
  rather than counting those bytes as freed. Keep the tree `s1`-owned.
- `retention-selftest.sh` exercises the whole policy against a throwaway
  fixture (files dated 3/8/30/60 days, protected files deliberately ancient).
  Run it after changing any rule. It never touches the real folder.
- `housekeeping.log` rotates at 128 KB with one `.1` kept — 256 KB ceiling.
