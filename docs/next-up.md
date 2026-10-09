## Known remaining gaps (carried forward, deliberate)

- `Process::wait` still has no timeout (06-09 M5); a hanging engine blocks.
- `EngineInvocation` still mixes config and context (06-09 M1), much smaller
  after `recorder`/`auxDir` removal.
