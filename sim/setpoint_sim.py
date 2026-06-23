#!/usr/bin/env python3
"""Simulate the Balboa GS501Z single-temp-button setpoint UI + a closed-loop
controller, so the write-path algorithm can be validated before the PC817
optocoupler arrives.

BUTTON TRUTH TABLE (measured live on the panel 2026-06-23):
  - One temp button. Step = 1 degC per value-changing press.
  - Range 26..40 degC. At a limit it REVERSES (bounce) -- it does NOT wrap.
  - First press of a session REVEALS the current setpoint (no change) -- a free
    look. Confirmed: two separate wake-presses both showed 34, unchanged.
  - The value-changing direction TOGGLES vs the previous session.
  - Pressing keeps the session alive (a ~29-press bounce was one session); set
    mode commits the shown value ~2-3 s (about 4 display flashes) after the LAST
    press, then reverts to showing water temp.

CONTROL STRATEGY -- "ride the bounce":
  Direction only reverses at a limit or a new session, and we can't know the
  toggled start direction in advance. So we don't predict it: wake to read the
  setpoint, then keep pressing and watching. If we start moving away from the
  target we ride to the near limit, bounce, and come back. We stop the instant
  the value equals the target and let set mode time out to commit it. This
  converges from any start to any target because the bounce sweeps the whole
  range. No direction prediction, no reliance on the toggle rule.
"""

LO, HI = 26, 40


class SpaPanel:
    """Models the panel's setpoint UI per the truth table above."""

    def __init__(self, setpoint, last_session_dir=+1):
        self.sp = setpoint                 # committed setpoint
        self.last_session_dir = last_session_dir
        self.in_setmode = False
        self.shown = None                  # value flashing while in set mode
        self.dir = None                    # this session's step direction
        self.presses = 0                   # physical presses issued

    def press(self):
        self.presses += 1
        if not self.in_setmode:
            # Wake: reveal current setpoint, no change. Session direction is the
            # toggle of the previous session's direction (unknown to the caller).
            self.in_setmode = True
            self.shown = self.sp
            self.dir = -self.last_session_dir
            return self.shown
        # Value-changing press, with bounce at the limits.
        nxt = self.shown + self.dir
        if nxt > HI:
            self.dir = -1
            nxt = HI - 1
        elif nxt < LO:
            self.dir = +1
            nxt = LO + 1
        self.shown = nxt
        return self.shown

    def settle(self):
        """Stop pressing -> set mode times out, commits the shown value."""
        if self.in_setmode:
            self.sp = self.shown
            self.last_session_dir = self.dir
            self.in_setmode = False
            self.shown = None
        return self.sp


def reach(panel, target, trace=None):
    """Ride-the-bounce controller. Returns presses used to land on target."""
    target = max(LO, min(HI, target))
    start = panel.presses
    v = panel.press()                      # wake / reveal (free look)
    if trace is not None:
        trace.append(("wake -> read", v))
    guard = 0
    while v != target:
        v = panel.press()
        if trace is not None:
            trace.append(("press", v))
        guard += 1
        if guard > 200:                    # safety: must never happen
            raise RuntimeError("did not converge -- model bug")
    panel.settle()                         # commit target
    if trace is not None:
        trace.append(("commit", panel.sp))
    return panel.presses - start


def example(start, target, last_dir):
    panel = SpaPanel(start, last_session_dir=last_dir)
    tr = []
    n = reach(panel, target, trace=tr)
    seq = " ".join(str(v) for _, v in tr if _ in ("wake -> read", "press"))
    arrow = "toward" if (-last_dir > 0) == (target - start > 0) else "AWAY (rides bounce)"
    print(f"  {start} -> {target}  (first move {arrow}): {n} presses")
    print(f"      {seq}  =[commit {target}]")
    return n


def worst_case():
    worst = 0
    for last_dir in (+1, -1):
        for s in range(LO, HI + 1):
            for t in range(LO, HI + 1):
                p = SpaPanel(s, last_session_dir=last_dir)
                worst = max(worst, reach(p, t))
    return worst


if __name__ == "__main__":
    print("Balboa GS501Z setpoint controller -- simulation\n")
    print("Representative runs (last_dir = the previous session's direction):\n")
    # current setpoint 34, water 36; shed scenarios + a wrong-way case
    example(34, 30, last_dir=+1)   # nudge target down for price shed
    example(34, 38, last_dir=-1)   # raise target
    example(34, 34, last_dir=+1)   # already on target (just a confirm)
    example(26, 40, last_dir=+1)   # floor to ceiling
    example(30, 28, last_dir=-1)   # first move away -> rides to ceiling, back

    cadence = 1.2                  # s/press: paced so the decoder can read between
    commit_wait = 3.0              # s for set mode to time out + commit
    w = worst_case()
    print(f"\nWorst case over ALL (start, target, direction): {w} presses")
    print(f"  ~= {w * cadence + commit_wait:.0f}s at {cadence}s/press + {commit_wait:.0f}s commit")
    print("  (one session -- pressing keeps it alive; never times out mid-run)")
