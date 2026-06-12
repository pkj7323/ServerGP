# Project: Player Death Logic and UI
# Scope: Server and Client death logic + EXP UI

## Architecture
- **Server**: Handle player death, deduct 10% of total exp for current level (bounded to 0 min), change state to wait for 5 seconds, then respawn at (1000, 1000) and update client.
- **Client**: Detect death state, show 5s countdown and dark screen. On respawn, remove UI. Update EXP UI to show current / max exp.
- **Network**: Server needs to notify client of death (if not already handled) and exp updates.

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | Server Death Logic | 5s wait, 10% exp drop, respawn at 1000,1000 | none | IN_PROGRESS |
| 2 | Client Death UI & EXP | 5s countdown, dark screen, "cur/max" exp UI | none | DONE |

## Interface Contracts
### Server ↔ Client
- Need to check `Protocol.h` for packet definitions related to player stat updates (exp, hp) and death/respawn states.

## Code Layout
- Server source: `d:\GitHub\ServerGP\텀프로젝트\Server\Server`
- Client source: `d:\GitHub\ServerGP\텀프로젝트\Client\Client`
- Shared/Protocol: `Protocol.h` (seems identically named in both, or one is copied from another)
