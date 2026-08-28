"use client";

import { useCallback, useEffect, useRef, useState } from "react";

type TurboControl = "mouse" | "space";
type Toast = { message: string; kind: "ok" | "error" } | null;

const commandLabels: Record<string, string> = {
  ubuntu: "Ubuntu terminal launched",
  cmd: "Windows command prompt launched",
  altf4: "Alt + F4 sent",
};

export default function Home() {
  const [active, setActive] = useState<Record<TurboControl, boolean>>({ mouse: false, space: false });
  const [walking, setWalking] = useState(false);
  const [toast, setToast] = useState<Toast>(null);
  const toastTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const activeRef = useRef(active);

  useEffect(() => { activeRef.current = active; }, [active]);

  const notify = useCallback((message: string, kind: "ok" | "error" = "ok") => {
    setToast({ message, kind });
    if (toastTimer.current) clearTimeout(toastTimer.current);
    toastTimer.current = setTimeout(() => setToast(null), 2200);
  }, []);

  const send = useCallback(async (path: string) => {
    try {
      const response = await fetch(path, { method: "GET", cache: "no-store" });
      if (!response.ok) throw new Error("Command rejected");
      return true;
    } catch {
      notify("Pico did not respond — check Wi-Fi", "error");
      return false;
    }
  }, [notify]);

  const setTurbo = useCallback(async (control: TurboControl, isActive: boolean) => {
    if (activeRef.current[control] === isActive) return;
    activeRef.current = { ...activeRef.current, [control]: isActive };
    setActive(activeRef.current);
    const ok = await send(`/${control}/${isActive ? "start" : "stop"}`);
    if (!ok && isActive) {
      activeRef.current = { ...activeRef.current, [control]: false };
      setActive(activeRef.current);
    }
  }, [send]);

  useEffect(() => {
    const releaseAll = () => {
      if (activeRef.current.mouse) void setTurbo("mouse", false);
      if (activeRef.current.space) void setTurbo("space", false);
    };
    window.addEventListener("blur", releaseAll);
    document.addEventListener("visibilitychange", releaseAll);
    return () => {
      window.removeEventListener("blur", releaseAll);
      document.removeEventListener("visibilitychange", releaseAll);
    };
  }, [setTurbo]);

  const runCommand = async (command: "ubuntu" | "cmd" | "altf4") => {
    if (await send(`/${command}`)) notify(commandLabels[command]);
  };

  const toggleWalk = async () => {
    const next = !walking;
    setWalking(next);
    if (await send("/walk/toggle")) notify(next ? "AFK walk engaged" : "AFK walk disengaged");
    else setWalking(!next);
  };

  const stopAll = async () => {
    activeRef.current = { mouse: false, space: false };
    setActive(activeRef.current);
    setWalking(false);
    if (await send("/stop")) notify("All macros stopped");
  };

  const holdProps = (control: TurboControl) => ({
    onPointerDown: (event: React.PointerEvent<HTMLButtonElement>) => {
      event.preventDefault();
      event.currentTarget.setPointerCapture(event.pointerId);
      void setTurbo(control, true);
    },
    onPointerUp: () => void setTurbo(control, false),
    onPointerCancel: () => void setTurbo(control, false),
    onLostPointerCapture: () => void setTurbo(control, false),
    onKeyDown: (event: React.KeyboardEvent<HTMLButtonElement>) => {
      if ((event.key === " " || event.key === "Enter") && !event.repeat) {
        event.preventDefault();
        void setTurbo(control, true);
      }
    },
    onKeyUp: (event: React.KeyboardEvent<HTMLButtonElement>) => {
      if (event.key === " " || event.key === "Enter") void setTurbo(control, false);
    },
  });

  const live = active.mouse || active.space || walking;

  return (
    <main className="shell">
      <div className="ambient ambient-one" />
      <div className="ambient ambient-two" />
      <section className="console" aria-label="Pico 2W macro controller">
        <header className="topbar">
          <div className="brand">
            <div className="brand-mark" aria-hidden="true"><span>P2</span></div>
            <div><p className="eyebrow">WIRELESS HID CONTROLLER</p><h1>Pico <span>2W</span> Macro</h1></div>
          </div>
          <div className={`connection ${live ? "live" : ""}`}><i aria-hidden="true" /><span>{live ? "MACRO LIVE" : "SYSTEM READY"}</span></div>
        </header>

        <div className="telemetry" aria-label="System status">
          <div><span>DEVICE</span><strong>PICO 2W</strong></div>
          <div><span>LINK</span><strong className="cyan">Wi-Fi</strong></div>
          <div><span>MODE</span><strong>{live ? "ACTIVE" : "STANDBY"}</strong></div>
        </div>

        <section className="control-section">
          <div className="section-heading"><div><span>01</span><h2>Turbo controls</h2></div><p>Press and hold to engage</p></div>
          <div className="turbo-grid">
            <button className={`macro-button turbo mouse ${active.mouse ? "active" : ""}`} {...holdProps("mouse")} aria-pressed={active.mouse}>
              <span className="button-icon mouse-icon" aria-hidden="true"><i /></span>
              <span className="button-copy"><small>HOLD TO ACTIVATE</small><strong>Mouse Turbo</strong></span>
              <span className="hold-indicator" aria-hidden="true">{active.mouse ? "LIVE" : "HOLD"}</span>
            </button>
            <button className={`macro-button turbo space ${active.space ? "active" : ""}`} {...holdProps("space")} aria-pressed={active.space}>
              <span className="button-icon key-icon" aria-hidden="true">SPC</span>
              <span className="button-copy"><small>HOLD TO ACTIVATE</small><strong>Space Turbo</strong></span>
              <span className="hold-indicator" aria-hidden="true">{active.space ? "LIVE" : "HOLD"}</span>
            </button>
          </div>
        </section>

        <section className="control-section utility-section">
          <div className="section-heading"><div><span>02</span><h2>Quick actions</h2></div><p>One-tap commands</p></div>
          <div className="action-grid">
            <button className={`macro-button walk ${walking ? "active" : ""}`} onClick={toggleWalk} aria-pressed={walking}>
              <span className="button-icon walk-icon" aria-hidden="true">W</span><span className="button-copy"><small>TOGGLE MODE</small><strong>AFK Walk</strong></span><span className="toggle" aria-hidden="true"><i /></span>
            </button>
            <button className="macro-button command" onClick={() => runCommand("ubuntu")}>
              <span className="os-icon ubuntu-icon" aria-hidden="true">⌘</span><span className="button-copy"><small>LINUX</small><strong>Ubuntu Terminal</strong></span><span className="arrow" aria-hidden="true">↗</span>
            </button>
            <button className="macro-button command" onClick={() => runCommand("cmd")}>
              <span className="os-icon windows-icon" aria-hidden="true">⊞</span><span className="button-copy"><small>WINDOWS</small><strong>Command Prompt</strong></span><span className="arrow" aria-hidden="true">↗</span>
            </button>
            <button className="macro-button command danger-command" onClick={() => runCommand("altf4")}>
              <span className="os-icon close-icon" aria-hidden="true">×</span><span className="button-copy"><small>WINDOW CONTROL</small><strong>Alt + F4</strong></span><span className="keys" aria-hidden="true"><kbd>ALT</kbd><kbd>F4</kbd></span>
            </button>
          </div>
        </section>

        <button className="stop-button" onClick={stopAll}>
          <span className="stop-icon" aria-hidden="true"><i /></span><span><small>EMERGENCY OVERRIDE</small><strong>STOP ALL MACROS</strong></span><span className="stop-key" aria-hidden="true">ESC</span>
        </button>
        <footer><span><i /> SECURE LOCAL CONNECTION</span><span>RP2350 · HID ENGINE</span><span>CORE TEMP <b>32°C</b></span></footer>
      </section>
      <div className={`toast ${toast ? "visible" : ""} ${toast?.kind === "error" ? "error" : ""}`} role="status" aria-live="polite">
        <i aria-hidden="true">{toast?.kind === "error" ? "!" : "✓"}</i>{toast?.message}
      </div>
    </main>
  );
}
