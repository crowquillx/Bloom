# Parallel agent kickoff prompt (Flutter track + CEF track)

Use this as the initial prompt for a multi-agent coding assistant.

```text
You are coordinating two parallel workers to evaluate and start implementation for Bloom migration paths:

Goal
- Launch two parallel workstreams:
  1) Flutter path
  2) CEF path
- Maximize shared backend reuse between both paths.
- Preserve Bloom’s core HTPC requirements: keyboard/gamepad-first UX, reliable playback lifecycle/reporting, and Linux/Wayland practical support.

Critical context
- Existing Bloom architecture is C++-centric with service/controller logic already in native code.
- Keep as much backend as possible shared and framework-agnostic.
- UI should be treated as replaceable shell(s), not owners of core playback/business logic.

Execution model
- Spawn TWO workers in parallel with non-overlapping ownership:

Worker A (Flutter track) ownership
- Produce a concrete Flutter integration plan that uses a shared native backend layer.
- Define plugin/FFI boundary, data contracts, event streams, and control APIs.
- Build or scaffold a minimal vertical slice: login/session restore -> library listing -> playback start/pause/stop.
- Document Linux desktop focus/input handling strategy for 10-foot navigation.

Worker B (CEF track) ownership
- Produce a concrete CEF integration plan using the same shared native backend layer.
- Define C++ <-> JS binding boundary, typed command/event bridge, and lifecycle model.
- Build or scaffold the same minimal vertical slice for parity.
- Document keyboard/remote focus model and windowing/overlay implications.

Shared backend mandate (both workers)
- Propose and align on one shared "Bloom Core" interface that both shells call.
- The shared interface should include at minimum:
  - Auth/session
  - Library browse/search
  - Playback control/state
  - Reporting lifecycle (start/progress/pause/stop)
  - Track selection mapping + persistence hooks
  - Config/settings access
- Explicitly avoid duplicating backend logic in Dart or JS.

Required deliverables (from EACH worker)
1) Architecture note (short, concrete)
2) File-level implementation plan with ownership and milestones
3) Minimal vertical slice status (what runs now / what is stubbed)
4) Risk list + mitigations
5) Time estimate for:
   - feasibility prototype
   - first usable HTPC build
   - feature parity with Bloom

Coordinator output
After both workers return, produce:
- A side-by-side comparison table (Flutter vs CEF):
  - reuse percentage estimate (backend vs UI)
  - complexity
  - Linux readiness
  - playback risk
  - focus/navigation risk
  - maintainability
- A recommendation with explicit go/no-go criteria.
- A phased migration plan that starts with shared backend extraction first.

Constraints
- Do not block one worker on the other except for the shared backend interface agreement.
- Keep both workers aware they are not alone in the codebase and must not revert each other’s edits.
- Prefer additive scaffolding and interface-first changes.
- Keep docs updated as you create new interfaces/components.

Definition of success for this kickoff
- Both tracks can run a demonstrable vertical slice using the same backend interface contract.
- Clear evidence of how much backend can truly be shared.
- Decision-ready recommendation grounded in implemented scaffolding, not just opinion.
```
