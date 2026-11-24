---
name: Gameplay or build regression
about: Provide enough information to reproduce a failure on another checkout.
---

## Environment

Commit or tag, build or trace configuration, executable SHA-256, OS and wrapper settings. For a build failure, include the compiler and SDK versions printed by `dev.cmd doctor`.

## Reproduction

Starting state or private save identifier and hash, followed by the shortest known steps. Include frame or simulation rate if timing matters.

## Expected and observed behavior

Describe what happened and what the comparison build did with the same input. If an original 8.1 comparison is unavailable, state what the expectation is based on.

## Diagnostic excerpt

For a crash, include the exit code and relevant map address, event sequence or last visible frame. For a build failure, include the first failing command and error. Keep full build products, captures and saves in your local workspace.
