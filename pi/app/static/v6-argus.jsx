// V6 — ARGUS (autonomous-first, manual hidden in drawer)
// Brand: Argus, OMV navy #06275c. Logo is the X-drone with corner brackets,
// recreated as SVG. Layout: thin header → hero feed (massive) + right panel
// (Coverage + Live inspection details) → history strip. Manual control is
// a floating action button in the feed; opening it slides a drawer up over
// the bottom of the feed and shows an "Autonomous paused" banner.

// ─── Brand tokens ────────────────────────────────────────────────────
const OMV_NAVY = '#06275c';       // brand primary
const OMV_NAVY_DEEP = '#04193b';   // deeper / fills
const OMV_NAVY_HI = '#1d4a8f';     // ui highlight (active states)
const OMV_BLUE_GLOW = '#3b7dd8';   // bright signal accent for live indicators

const v6 = {
  canvas: '#06080c',
  surface: '#0c0f15',
  surfaceHi: '#11151d',
  inset: '#04060a',
  hair: 'rgba(238,232,222,0.07)',
  hairBright: 'rgba(238,232,222,0.14)',
  text: '#eef0f3',
  textDim: '#bdc1c9',
  mute: '#8a8f99',
  faint: '#4d525c',
  danger: '#e54545',
  warn: '#e5b94a',
  ok: '#5bba6f',
  mono: { fontFamily: '"JetBrains Mono", monospace' },
  brand: { fontFamily: '"Helvetica Neue", Helvetica, Arial, sans-serif', letterSpacing: 6, fontWeight: 500 },
};

// Shared data (from v6-data.jsx)
const { DETECTIONS, PRIORITY, STATUS } = window;

// Hold-to-jog hook (local to V6 to avoid scope collisions with V4)
function useV6Jog() {
  const [held, setHeld] = React.useState(null);
  const timer = React.useRef(null);
  const start = (id) => {
    setHeld(id);
    let n = 0;
    timer.current = setInterval(() => { n++; }, 100);
  };
  const stop = () => {
    setHeld(null);
    if (timer.current) { clearInterval(timer.current); timer.current = null; }
  };
  return { held, start, stop };
}

// ─── Argus Logo (Pod with iris — D from Argus Logo.html) ──────────────
function ArgusLogo({ size = 24, color = '#fff', bg = v6.surface }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" fill="none">
      {/* 4 corner brackets */}
      <path d="M 6 6 L 6 14 M 6 6 L 14 6"     stroke={color} strokeWidth="3" strokeLinecap="square" />
      <path d="M 58 6 L 58 14 M 58 6 L 50 6"   stroke={color} strokeWidth="3" strokeLinecap="square" />
      <path d="M 6 58 L 6 50 M 6 58 L 14 58"   stroke={color} strokeWidth="3" strokeLinecap="square" />
      <path d="M 58 58 L 58 50 M 58 58 L 50 58" stroke={color} strokeWidth="3" strokeLinecap="square" />
      {/* Cables from brackets to pod corners */}
      <line x1="11" y1="11" x2="22" y2="22" stroke={color} strokeWidth="1.8" />
      <line x1="53" y1="11" x2="42" y2="22" stroke={color} strokeWidth="1.8" />
      <line x1="11" y1="53" x2="22" y2="42" stroke={color} strokeWidth="1.8" />
      <line x1="53" y1="53" x2="42" y2="42" stroke={color} strokeWidth="1.8" />
      {/* Pod body with chamfered bottom */}
      <path d="M 22 22 L 42 22 L 42 38 Q 42 42 38 42 L 26 42 Q 22 42 22 38 Z" fill={color} />
      {/* Iris well (cut-out, painted in surface bg to read as a hole) */}
      <circle cx="32" cy="31" r="6.5" fill={bg} />
      {/* Three-bladed iris */}
      <g fill={color} transform="translate(32 31)">
        <path d="M 0 -5.5 L 3.3 2.2 L -3.3 2.2 Z" />
        <path d="M 0 -5.5 L 3.3 2.2 L -3.3 2.2 Z" transform="rotate(120)" />
        <path d="M 0 -5.5 L 3.3 2.2 L -3.3 2.2 Z" transform="rotate(240)" />
      </g>
      {/* Center dot */}
      <circle cx="32" cy="31" r="1" fill={bg} />
    </svg>
  );
}

// ─── Header ──────────────────────────────────────────────────────────
function V6Header() {
  const statuses = [
    { key: 'esp32', label: 'ESP32 Motor Controller', detail: '192.168.4.1 · -52 dBm', state: 'ok' },
    { key: 'pi', label: 'Raspberry Pi', detail: '29.7 fps stream · -42 dBm', state: 'ok' },
    { key: 'motors', label: 'Motors', detail: '4 cables · 0.42 A', state: 'ok' },
    { key: 'ir', label: 'IR Sensor', detail: 'Calibrating · 24.0°C ambient', state: 'warn' },
  ];
  const [hovered, setHovered] = React.useState(null);
  return (
    <div style={{
      height: 52, flex: '0 0 52px',
      display: 'flex', alignItems: 'stretch',
      borderBottom: `1px solid ${v6.hair}`,
      background: v6.surface,
      position: 'relative', zIndex: 30,
    }}>
      {/* Brand */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '0 22px', borderRight: `1px solid ${v6.hair}` }}>
        <ArgusLogo size={26} color="#fff" />
        <div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center' }}>
          <div style={{ ...v6.brand, fontSize: 15, color: '#fff', lineHeight: 1 }}>ARGUS</div>
          <div style={{ ...v6.mono, fontSize: 8.5, color: v6.mute, letterSpacing: 1.8, marginTop: 5 }}>THERMAL INSPECTION · OMV</div>
        </div>
      </div>

      {/* Mission center */}
      <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 24, ...v6.mono, fontSize: 10.5, color: v6.mute, letterSpacing: 1.5 }}>
        <span><span style={{ color: v6.faint, marginRight: 6 }}>MISSION</span><span style={{ color: v6.text }}>OMV-A · GAS DISTRIBUTION HUB 4</span></span>
        <span style={{ color: v6.faint }}>·</span>
        <span><span style={{ color: v6.faint, marginRight: 6 }}>SESSION</span><span style={{ color: v6.text }}>47</span></span>
        <span style={{ color: v6.faint }}>·</span>
        <span style={{ color: v6.text }}>14:22:08 UTC</span>
      </div>

      {/* Status icons */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 4, paddingRight: 14, borderLeft: `1px solid ${v6.hair}`, paddingLeft: 14 }}>
        {statuses.map(s => {
          const color = s.state === 'ok' ? v6.ok : s.state === 'warn' ? v6.warn : v6.danger;
          const icon = s.key === 'esp32' || s.key === 'motors' ? <IconChip size={14} />
            : s.key === 'pi' ? <IconWifi size={14} /> : <IconThermal size={14} />;
          return (
            <div key={s.key}
              onMouseEnter={() => setHovered(s.key)} onMouseLeave={() => setHovered(null)}
              style={{ position: 'relative', display: 'flex', alignItems: 'center', justifyContent: 'center', width: 34, height: 34, cursor: 'pointer', background: hovered === s.key ? v6.surfaceHi : 'transparent', color: v6.text }}>
              {icon}
              <div style={{ position: 'absolute', bottom: 5, right: 5, width: 6, height: 6, borderRadius: '50%', background: color, boxShadow: `0 0 6px ${color}`, border: `1.5px solid ${v6.surface}` }} />
              {hovered === s.key && (
                <div style={{
                  position: 'absolute', top: '100%', right: 0, marginTop: 4,
                  background: '#000', border: `1px solid ${v6.hairBright}`,
                  padding: '8px 12px', whiteSpace: 'nowrap',
                  zIndex: 40, boxShadow: '0 8px 24px rgba(0,0,0,0.6)',
                }}>
                  <div style={{ fontSize: 11.5, color: '#fff' }}>{s.label}</div>
                  <div style={{ ...v6.mono, fontSize: 9.5, color: v6.mute, marginTop: 2, letterSpacing: 1 }}>{s.detail}</div>
                </div>
              )}
            </div>
          );
        })}
      </div>

      {/* E-stop, hangs past the bar */}
      <button style={{
        width: 148, height: 80,
        background: 'linear-gradient(180deg, #c91a1a 0%, #8a0d0d 100%)',
        color: '#fff', border: 'none',
        borderLeft: `1px solid ${v6.hair}`,
        cursor: 'pointer',
        display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 10,
        boxShadow: 'inset 0 -3px 0 rgba(0,0,0,0.45), inset 0 1px 0 rgba(255,255,255,0.15), 0 4px 16px rgba(201,26,26,0.25)',
        zIndex: 30,
      }}>
        <div style={{ width: 30, height: 30, borderRadius: '50%', background: 'radial-gradient(circle at 35% 30%, #ff5a5a, #8a0d0d 70%)', border: '2px solid #fff', boxShadow: '0 0 0 2.5px rgba(201,26,26,0.6)' }} />
        <div style={{ textAlign: 'left' }}>
          <div style={{ fontSize: 14, fontWeight: 700, letterSpacing: 2 }}>E-STOP</div>
          <div style={{ ...v6.mono, fontSize: 8.5, opacity: 0.75, letterSpacing: 1.2 }}>HOLD TO ABORT</div>
        </div>
      </button>
    </div>
  );
}

// ─── Coverage block in right rail ────────────────────────────────────
function V6CoverageBlock({ onAnomalyHover, anomalies, hoverAnomaly }) {
  return (
    <div style={{ padding: '14px 18px 12px', borderBottom: `1px solid ${v6.hair}` }}>
      <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', marginBottom: 10 }}>
        <span style={{ ...v6.mono, fontSize: 9.5, letterSpacing: 2.5, color: v6.mute, textTransform: 'uppercase' }}>Coverage</span>
        <span style={{ ...v6.mono, fontSize: 9, color: v6.faint, letterSpacing: 1.2 }}>800 × 800 mm</span>
      </div>
      <div style={{ display: 'flex', gap: 16, alignItems: 'stretch' }}>
        {/* Square map on the left */}
        <div style={{ width: 158, flex: '0 0 158px', aspectRatio: '1 / 1', background: v6.inset, position: 'relative' }}>
          <V6SweptCoverage progress={0.42} anomalies={anomalies} onAnomalyHover={onAnomalyHover} />
          {hoverAnomaly && (
            <div style={{
              position: 'absolute',
              left: `calc(${hoverAnomaly.x * 100}% + 10px)`,
              top: `calc(${hoverAnomaly.y * 100}% - 56px)`,
              background: '#000', border: `1px solid rgba(229,69,69,0.5)`,
              padding: 6, zIndex: 5,
              boxShadow: '0 8px 24px rgba(0,0,0,0.7)',
              display: 'flex', gap: 8, alignItems: 'center',
            }}>
              <div style={{ width: 52, height: 40, overflow: 'hidden' }}>
                <HistoryThumb mode="diff" anomaly={true} w={52} h={40} />
              </div>
              <div>
                <div style={{ ...v6.mono, fontSize: 10, color: '#ff8a8a', letterSpacing: 1 }}>{hoverAnomaly.id} · {hoverAnomaly.temp}</div>
                <div style={{ ...v6.mono, fontSize: 9, color: v6.mute, marginTop: 2, letterSpacing: 1 }}>PASS {hoverAnomaly.pass} · {hoverAnomaly.time}</div>
              </div>
            </div>
          )}
        </div>
        {/* Vertically-stacked stats on the right */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', justifyContent: 'space-between', padding: '2px 0' }}>
          {[
            ['SWEPT', '42%', v6.text],
            ['FLAGGED', '02', '#ff7070'],
            ['ETA', '05:51', v6.text],
          ].map(([k, v, c]) => (
            <div key={k}>
              <div style={{ ...v6.mono, fontSize: 8.5, color: v6.faint, letterSpacing: 1.8 }}>{k}</div>
              <div style={{ ...v6.mono, fontWeight: 500, fontSize: 22, color: c, marginTop: 3, letterSpacing: -0.3 }}>{v}</div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}

// Swept region map (same algorithm as V4/V5 but using OMV navy)
function V6SweptCoverage({ progress = 0.42, anomalies = [], onAnomalyHover, w = 240, h = 240 }) {
  const rows = 6;
  const margin = 6;
  const innerW = w - margin * 2;
  const innerH = h - margin * 2;
  const rowH = innerH / rows;
  const head = progress * rows;
  const fullRows = Math.floor(head);
  const partial = head - fullRows;
  const goingRight = fullRows % 2 === 0;
  const headX = margin + (goingRight ? partial * innerW : (1 - partial) * innerW);
  const headY = margin + fullRows * rowH + rowH / 2;
  return (
    <svg viewBox={`0 0 ${w} ${h}`} width="100%" height="100%" style={{ display: 'block' }}>
      <rect width={w} height={h} fill={v6.inset} />
      <rect x={margin} y={margin} width={innerW} height={innerH} fill="none" stroke="rgba(255,255,255,0.07)" strokeDasharray="2 3" />
      {/* swept rows */}
      {Array.from({ length: fullRows }).map((_, r) => (
        <rect key={r} x={margin} y={margin + r * rowH} width={innerW} height={rowH}
          fill={OMV_NAVY_HI} opacity={0.30 + (r / Math.max(fullRows, 1)) * 0.20} />
      ))}
      {partial > 0 && (
        <rect
          x={goingRight ? margin : margin + (1 - partial) * innerW}
          y={margin + fullRows * rowH}
          width={partial * innerW} height={rowH}
          fill={OMV_NAVY_HI} opacity="0.65"
        />
      )}
      {/* anomalies */}
      {anomalies.map((a, i) => (
        <g key={i} style={{ cursor: 'pointer' }}
          onMouseEnter={() => onAnomalyHover?.(a)}
          onMouseLeave={() => onAnomalyHover?.(null)}>
          <circle cx={margin + a.x * innerW} cy={margin + a.y * innerH} r="9" fill="rgba(229,69,69,0.18)">
            <animate attributeName="r" values="7;11;7" dur="2s" repeatCount="indefinite" />
          </circle>
          <circle cx={margin + a.x * innerW} cy={margin + a.y * innerH} r="4" fill="#ff5a5a" stroke="#fff" strokeWidth="1.3" />
        </g>
      ))}
      {/* head */}
      <g>
        <circle cx={headX} cy={headY} r="9" fill={`${OMV_BLUE_GLOW}40`}>
          <animate attributeName="r" values="7;11;7" dur="1.6s" repeatCount="indefinite" />
        </circle>
        <circle cx={headX} cy={headY} r="4" fill={OMV_BLUE_GLOW} stroke="#fff" strokeWidth="1.5" />
      </g>
    </svg>
  );
}

// ─── Live inspection details panel ────────────────────────────────────
function V6InspectionDetails({ running, onPauseResume }) {
  const progress = 0.42;
  return (
    <div style={{ flex: 1, padding: '14px 18px', display: 'flex', flexDirection: 'column', gap: 12, minHeight: 0, overflowY: 'auto' }}>
      <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between' }}>
        <span style={{ ...v6.mono, fontSize: 9.5, letterSpacing: 2.5, color: v6.mute, textTransform: 'uppercase' }}>Inspection</span>
        <span style={{
          ...v6.mono, fontSize: 9.5, letterSpacing: 1.5,
          color: running ? v6.ok : v6.warn,
          display: 'flex', alignItems: 'center', gap: 6,
        }}>
          <span style={{ width: 7, height: 7, borderRadius: '50%', background: running ? v6.ok : v6.warn, boxShadow: `0 0 8px ${running ? v6.ok : v6.warn}` }} />
          {running ? 'AUTONOMOUS' : 'PAUSED'}
        </span>
      </div>

      {/* Title + status (A1 voice) */}
      <div>
        <div style={{ fontFamily: '"Helvetica Neue", Helvetica, Arial, sans-serif', fontSize: 17, color: '#fff', lineHeight: 1.15, fontWeight: 500, letterSpacing: 0.6 }}>
          {running ? 'SCAN ACTIVE' : 'STANDING BY'}
        </div>
        <div style={{ ...v6.mono, fontSize: 9.5, color: v6.mute, marginTop: 6, letterSpacing: 1 }}>
          PASS 04 OF 12 &nbsp;·&nbsp; CELL 40 OF 96
        </div>
      </div>

      {/* Big progress number */}
      <div>
        <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}>
          <span style={{ fontFamily: '"Helvetica Neue", Helvetica, Arial, sans-serif', fontWeight: 200, fontSize: 44, color: '#fff', lineHeight: 0.9, letterSpacing: -1.5 }}>{Math.round(progress * 100)}</span>
          <span style={{ ...v6.mono, fontSize: 13, color: v6.mute }}>%</span>
          <div style={{ flex: 1 }} />
          <div style={{ ...v6.mono, fontSize: 9, color: v6.faint, letterSpacing: 1.2, textAlign: 'right' }}>
            <div>ELAPSED <span style={{ color: v6.text, ...v6.mono, fontWeight: 500, fontSize: 12, marginLeft: 4 }}>04:18</span></div>
            <div style={{ marginTop: 3 }}>ETA <span style={{ color: v6.text, ...v6.mono, fontWeight: 500, fontSize: 12, marginLeft: 4 }}>05:51</span></div>
          </div>
        </div>
        <div style={{ height: 3, background: 'rgba(255,255,255,0.05)', marginTop: 8, overflow: 'hidden' }}>
          <div style={{ height: '100%', width: `${progress * 100}%`, background: `linear-gradient(90deg, ${OMV_NAVY}, ${OMV_BLUE_GLOW})`, boxShadow: `0 0 10px ${OMV_BLUE_GLOW}` }} />
        </div>
      </div>

      {/* Live readouts — single 4-col row */}
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', borderTop: `1px solid ${v6.hair}`, borderBottom: `1px solid ${v6.hair}` }}>
        {[
          ['X', '0638', 'mm'],
          ['Y', '0472', 'mm'],
          ['Z', '1280', 'mm'],
          ['ANOM', '02', ''],
        ].map(([k, val, unit], i) => (
          <div key={k} style={{
            padding: '8px 6px',
            borderLeft: i > 0 ? `1px solid ${v6.hair}` : 'none',
          }}>
            <div style={{ ...v6.mono, fontSize: 8.5, color: v6.faint, letterSpacing: 1.5 }}>{k}</div>
            <div style={{ display: 'flex', alignItems: 'baseline', gap: 3, marginTop: 2 }}>
              <span style={{ ...v6.mono, fontWeight: 500, fontSize: 15, color: k === 'ANOM' ? '#ff7070' : '#fff', lineHeight: 1 }}>{val}</span>
              {unit && <span style={{ ...v6.mono, fontSize: 8.5, color: v6.faint }}>{unit}</span>}
            </div>
          </div>
        ))}
      </div>

      {/* Recent anomalies list */}
      <div>
        <div style={{ ...v6.mono, fontSize: 9, letterSpacing: 1.5, color: v6.faint, marginBottom: 6, textTransform: 'uppercase' }}>Recent anomalies</div>
        {[
          { id: 'A1', pass: '012', temp: '64.2°C', time: '14:18', coords: 'X 638 Y 472' },
          { id: 'A2', pass: '009', temp: '38.4°C', time: '14:00', coords: 'X 358 Y 198' },
        ].map(a => (
          <div key={a.id} style={{
            display: 'flex', alignItems: 'center', gap: 10,
            padding: '6px 0',
            borderBottom: `1px solid ${v6.hair}`,
            cursor: 'pointer',
          }}>
            <div style={{ width: 5, height: 5, borderRadius: '50%', background: '#ff7070', boxShadow: '0 0 6px #ff7070' }} />
            <div style={{ flex: 1 }}>
              <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}>
                <span style={{ fontSize: 12, color: '#fff', fontWeight: 500 }}>{a.id}</span>
                <span style={{ ...v6.mono, fontSize: 10.5, color: '#ff8a8a' }}>{a.temp}</span>
              </div>
              <div style={{ ...v6.mono, fontSize: 8.5, color: v6.faint, letterSpacing: 1, marginTop: 1 }}>{a.coords} · {a.time}</div>
            </div>
            <div style={{ ...v6.mono, fontSize: 8.5, color: v6.mute, letterSpacing: 1 }}>P{a.pass}</div>
          </div>
        ))}
      </div>

      <div style={{ flex: 1 }} />

      {/* Inspection controls */}
      <div style={{ display: 'flex', gap: 8 }}>
        <button onClick={onPauseResume} style={{
          flex: 1, padding: '10px 14px',
          background: running ? 'rgba(255,255,255,0.05)' : OMV_NAVY,
          border: `1px solid ${running ? v6.hair : OMV_NAVY_HI}`,
          color: '#fff', cursor: 'pointer',
          display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
          ...v6.mono, fontSize: 10.5, letterSpacing: 2, fontWeight: 600,
        }}>
          {running ? <><IconPause size={12} />PAUSE</> : <><IconPlay size={12} />RESUME</>}
        </button>
        <button style={{
          padding: '10px 14px',
          background: 'rgba(255,255,255,0.04)',
          border: `1px solid ${v6.hair}`,
          color: v6.mute, cursor: 'pointer',
          display: 'grid', placeItems: 'center',
        }}><IconStop size={12} /></button>
      </div>
    </div>
  );
}

// ─── Tab toggle (between Inspection and Manual) ─────────────────────
function V6TabToggle({ active, onChange }) {
  return (
    <div style={{
      display: 'grid', gridTemplateColumns: '1fr 1fr',
      padding: '10px 18px 0',
      gap: 0,
    }}>
      {[
        { key: 'inspection', label: 'INSPECTION' },
        { key: 'manual', label: 'MANUAL' },
      ].map(t => {
        const isActive = active === t.key;
        return (
          <button key={t.key} onClick={() => onChange(t.key)} style={{
            background: 'transparent',
            border: 'none',
            borderBottom: `1.5px solid ${isActive ? OMV_BLUE_GLOW : v6.hair}`,
            color: isActive ? '#fff' : v6.mute,
            padding: '7px 0',
            ...v6.mono, fontSize: 10.5, letterSpacing: 2.5,
            cursor: 'pointer',
            transition: 'color 0.15s, border-color 0.15s',
          }}>
            {t.label}
          </button>
        );
      })}
    </div>
  );
}

// ─── New manual control panel (inspiration-style buttons) ─────────────
function V6ManualPanel({ onResume }) {
  const [speed, setSpeed] = React.useState(45);
  const [zPos, setZPos] = React.useState(62);
  const jog = useV6Jog();

  return (
    <div style={{ flex: 1, padding: '14px 18px', display: 'flex', flexDirection: 'column', gap: 10, minHeight: 0, overflowY: 'auto' }}>
      {/* Status header + hint */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
        <span style={{ ...v6.mono, fontSize: 9.5, letterSpacing: 2.5, color: v6.mute, textTransform: 'uppercase' }}>Control</span>
        <span style={{
          ...v6.mono, fontSize: 9.5, letterSpacing: 1.5, color: v6.warn,
          display: 'flex', alignItems: 'center', gap: 6,
        }}>
          <span style={{ width: 7, height: 7, borderRadius: '50%', background: v6.warn, boxShadow: `0 0 8px ${v6.warn}` }} />
          AUTONOMOUS PAUSED
        </span>
      </div>

      <div style={{ ...v6.mono, fontSize: 9, color: v6.faint, letterSpacing: 1, textAlign: 'center' }}>
        TAP TO JOG · HOLD FOR CONTINUOUS
      </div>

      {/* Position readout — compact single inline row */}
      <div style={{
        display: 'flex', gap: 0,
        borderTop: `1px solid ${v6.hair}`,
        borderBottom: `1px solid ${v6.hair}`,
        padding: '8px 0',
      }}>
        {[
          ['X', '0842'],
          ['Y', '0314'],
          ['Z', '1280'],
        ].map(([axis, value], i) => (
          <div key={axis} style={{
            flex: 1,
            paddingLeft: i > 0 ? 12 : 0,
            borderLeft: i > 0 ? `1px solid ${v6.hair}` : 'none',
          }}>
            <div style={{ ...v6.mono, fontSize: 9, letterSpacing: 1.5, color: v6.faint }}>{axis} · MM</div>
            <div style={{ ...v6.mono, fontWeight: 500, fontSize: 17, color: '#fff', marginTop: 2, letterSpacing: -0.3 }}>{value}</div>
          </div>
        ))}
      </div>

      {/* Controls — X/Y pad + Z column */}
      <div style={{ display: 'flex', justifyContent: 'center', gap: 24, padding: '2px 0' }}>
        <div style={{
          display: 'grid',
          gridTemplateColumns: 'repeat(3, 46px)',
          gridTemplateRows: 'repeat(3, 36px)',
          gap: 5,
        }}>
          <div />
          <V6PillBtn id="up" jog={jog} dir="up" />
          <div />
          <V6PillBtn id="left" jog={jog} dir="left" />
          <V6HomeBtn id="home" jog={jog} />
          <V6PillBtn id="right" jog={jog} dir="right" />
          <div />
          <V6PillBtn id="down" jog={jog} dir="down" />
          <div />
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 3 }}>
          <div style={{ ...v6.mono, fontSize: 8.5, letterSpacing: 1.5, color: v6.mute, marginBottom: 1 }}>Z UP</div>
          <V6PillBtn id="zup" jog={jog} dir="up" width={46} height={32} />
          <V6ZBar value={zPos} onChange={setZPos} />
          <V6PillBtn id="zdown" jog={jog} dir="down" width={46} height={32} />
          <div style={{ ...v6.mono, fontSize: 8.5, letterSpacing: 1.5, color: v6.mute, marginTop: 1 }}>Z DN</div>
        </div>
      </div>

      {/* Speed slider */}
      <div>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', marginBottom: 6 }}>
          <span style={{ ...v6.mono, fontSize: 9.5, letterSpacing: 2, color: v6.mute, textTransform: 'uppercase' }}>Speed</span>
          <span style={{ ...v6.mono, fontWeight: 500, fontSize: 14, color: '#fff' }}>
            {speed}<span style={{ fontSize: 10, color: v6.faint, marginLeft: 3 }}>%</span>
          </span>
        </div>
        <div style={{ position: 'relative', height: 14, display: 'flex', alignItems: 'center' }}>
          <div style={{ width: '100%', height: 2, background: 'rgba(255,255,255,0.06)' }}>
            <div style={{ height: '100%', width: `${speed}%`, background: OMV_BLUE_GLOW }} />
          </div>
          <input type="range" min="0" max="100" value={speed} onChange={(e) => setSpeed(+e.target.value)} style={{ position: 'absolute', inset: 0, opacity: 0, cursor: 'pointer', width: '100%' }} />
          <div style={{ position: 'absolute', left: `calc(${speed}% - 5px)`, width: 10, height: 10, background: '#fff', borderRadius: '50%' }} />
        </div>
      </div>

      <div style={{ flex: 1, minHeight: 4 }} />

      {/* Resume autonomous */}
      <button onClick={onResume} style={{
        padding: '10px 14px',
        background: OMV_NAVY,
        border: `1px solid ${OMV_NAVY_HI}`,
        color: '#fff', cursor: 'pointer',
        display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
        ...v6.mono, fontSize: 10.5, letterSpacing: 2, fontWeight: 600,
      }}>
        <IconPlay size={12} />RESUME AUTONOMOUS
      </button>
    </div>
  );
}

// Pill-style jog button (rounded rectangle, subtle fill)
function V6PillBtn({ id, jog, dir, width, height }) {
  const isHeld = jog.held === id;
  const arrow = {
    up: 'M5 14 L10 6 L15 14 Z',
    down: 'M5 6 L15 6 L10 14 Z',
    left: 'M14 5 L14 15 L6 10 Z',
    right: 'M6 5 L6 15 L14 10 Z',
  }[dir];
  return (
    <button
      onMouseDown={() => jog.start(id)} onMouseUp={jog.stop} onMouseLeave={jog.stop}
      onTouchStart={(e) => { e.preventDefault(); jog.start(id); }} onTouchEnd={jog.stop}
      style={{
        position: 'relative',
        width: width || '100%', height: height || '100%',
        background: isHeld ? OMV_NAVY_HI : 'rgba(255,255,255,0.025)',
        border: `1px solid ${isHeld ? OMV_BLUE_GLOW : 'rgba(255,255,255,0.08)'}`,
        borderRadius: 6,
        color: isHeld ? '#fff' : '#a8aebb',
        cursor: 'pointer',
        display: 'flex', alignItems: 'center', justifyContent: 'center',
        transition: 'background 0.1s, border-color 0.1s, color 0.1s',
        padding: 0,
      }}
    >
      <svg width="20" height="20" viewBox="0 0 20 20" fill="currentColor">
        <path d={arrow} />
      </svg>
    </button>
  );
}

// Center "XY" button (home)
function V6HomeBtn({ id, jog }) {
  const isHeld = jog.held === id;
  return (
    <button
      onMouseDown={() => jog.start(id)} onMouseUp={jog.stop} onMouseLeave={jog.stop}
      onTouchStart={(e) => { e.preventDefault(); jog.start(id); }} onTouchEnd={jog.stop}
      style={{
        position: 'relative',
        width: '100%', height: '100%',
        background: isHeld ? OMV_NAVY_HI : 'rgba(29,74,143,0.18)',
        border: `1.5px solid ${isHeld ? OMV_BLUE_GLOW : 'rgba(59,125,216,0.5)'}`,
        borderRadius: 6,
        color: '#cfd4dc',
        cursor: 'pointer',
        ...v6.mono, fontSize: 11, fontWeight: 600, letterSpacing: 1.5,
        transition: 'background 0.1s, border-color 0.1s, color 0.1s',
        padding: 0,
      }}
    >
      <IconHome size={16} />
    </button>
  );
}

// Vertical Z position bar — draggable
function V6ZBar({ value, onChange }) {
  const ref = React.useRef(null);
  const dragging = React.useRef(false);
  const update = React.useCallback((e) => {
    if (!ref.current) return;
    const rect = ref.current.getBoundingClientRect();
    const y = (e.touches ? e.touches[0].clientY : e.clientY) - rect.top;
    const pct = Math.max(0, Math.min(100, 100 - (y / rect.height) * 100));
    onChange(Math.round(pct));
  }, [onChange]);
  React.useEffect(() => {
    const move = (e) => { if (dragging.current) update(e); };
    const up = () => { dragging.current = false; };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
    window.addEventListener('touchmove', move);
    window.addEventListener('touchend', up);
    return () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
      window.removeEventListener('touchmove', move);
      window.removeEventListener('touchend', up);
    };
  }, [update]);
  return (
    <div
      ref={ref}
      onMouseDown={(e) => { dragging.current = true; update(e); }}
      onTouchStart={(e) => { dragging.current = true; update(e); }}
      style={{
        width: 24, height: 48,
        background: 'rgba(0,0,0,0.4)',
        border: '1px solid rgba(255,255,255,0.06)',
        borderRadius: 4,
        position: 'relative',
        cursor: 'ns-resize',
        margin: '4px 0',
      }}
    >
      {/* Fill */}
      <div style={{
        position: 'absolute', left: 0, right: 0, bottom: 0,
        height: `${value}%`,
        background: `linear-gradient(180deg, ${OMV_BLUE_GLOW}, ${OMV_NAVY})`,
        borderRadius: '0 0 3px 3px',
      }} />
      {/* Handle */}
      <div style={{
        position: 'absolute', left: -3, right: -3, bottom: `${value}%`,
        transform: 'translateY(50%)',
        height: 4,
        background: '#fff',
        boxShadow: '0 1px 4px rgba(0,0,0,0.6)',
      }} />
      {/* Tick lines */}
      <div style={{ position: 'absolute', inset: 0, pointerEvents: 'none' }}>
        {[25, 50, 75].map(p => (
          <div key={p} style={{ position: 'absolute', left: 0, right: 0, top: `${p}%`, height: 1, background: 'rgba(255,255,255,0.06)' }} />
        ))}
      </div>
    </div>
  );
}

// ─── Right rail ──────────────────────────────────────────────────────
function V6RightRail({ activeTab, onTabChange, running, onPauseResume }) {
  const [hoverAnomaly, setHoverAnomaly] = React.useState(null);
  const anomalies = [
    { id: 'A1', x: 0.32, y: 0.72, temp: '64.2°C', pass: '012', time: '14:18' },
    { id: 'A2', x: 0.18, y: 0.30, temp: '38.4°C', pass: '009', time: '14:00' },
  ];
  return (
    <div style={{
      width: 360, flex: '0 0 360px',
      borderLeft: `1px solid ${v6.hair}`,
      background: v6.surface,
      display: 'flex', flexDirection: 'column',
      minHeight: 0,
    }}>
      <V6CoverageBlock onAnomalyHover={setHoverAnomaly} anomalies={anomalies} hoverAnomaly={hoverAnomaly} />
      <V6TabToggle active={activeTab} onChange={onTabChange} />
      {activeTab === 'inspection'
        ? <V6InspectionDetails running={running} onPauseResume={onPauseResume} />
        : <V6ManualPanel onResume={() => onTabChange('inspection')} />
      }
    </div>
  );
}

// ─── Hero feed with light HUD + manual FAB ───────────────────────────
function V6HeroFeed() {
  const [mode, setMode] = React.useState('thermal');
  return (
    <div style={{ flex: 1, position: 'relative', background: '#000', overflow: 'hidden', minWidth: 0 }}>
      <ThermalFeed mode={mode} anomaly={true} showCrosshair={true} showBrackets={false} label={null} />
      <div style={{ position: 'absolute', inset: 0, pointerEvents: 'none',
        background: 'radial-gradient(ellipse at center, transparent 35%, rgba(0,0,0,0.5) 100%)' }} />

      {/* A1: corner brackets */}
      <V6FeedCorners />

      {/* Mode toggle */}
      <div style={{
        position: 'absolute', top: 16, left: '50%', transform: 'translateX(-50%)',
        display: 'flex',
        background: 'rgba(6,7,10,0.78)',
        backdropFilter: 'blur(14px)', WebkitBackdropFilter: 'blur(14px)',
        border: `1px solid ${v6.hair}`, padding: 3,
        zIndex: 6,
      }}>
        {['thermal', 'normal'].map(m => (
          <button key={m} onClick={() => setMode(m)} style={{
            padding: '7px 18px',
            background: mode === m ? OMV_NAVY_HI : 'transparent',
            color: mode === m ? '#fff' : '#bcc1cc',
            border: 'none',
            ...v6.mono, fontSize: 10.5, letterSpacing: 1.2, textTransform: 'uppercase',
            cursor: 'pointer',
            display: 'flex', alignItems: 'center', gap: 6,
          }}>
            {m === 'thermal' ? <IconThermal size={12} /> : <IconCamera size={12} />}
            {m}
          </button>
        ))}
      </div>

      {/* Anomaly live banner */}
      <div style={{
        position: 'absolute', top: 16, right: 16,
        padding: '7px 12px',
        background: 'rgba(229,69,69,0.94)',
        border: '1px solid #ff5a5a',
        display: 'flex', alignItems: 'center', gap: 8,
        color: '#fff', zIndex: 8,
      }}>
        <div style={{ width: 7, height: 7, borderRadius: '50%', background: '#fff', animation: 'pulse 1.2s infinite' }} />
        <div style={{ ...v6.mono, fontSize: 10.5, letterSpacing: 1.2, fontWeight: 600 }}>ANOMALY · 64.2°C · LIVE</div>
      </div>

      {/* Temp tape — top left */}
      <div style={{
        position: 'absolute', top: 16, left: 16,
        background: 'rgba(6,7,10,0.6)',
        backdropFilter: 'blur(10px)', WebkitBackdropFilter: 'blur(10px)',
        border: `1px solid ${v6.hair}`,
        padding: '10px 14px',
        ...v6.mono, fontSize: 11, color: '#fff',
        zIndex: 6,
        display: 'flex', flexDirection: 'column', gap: 6,
      }}>
        <div style={{ display: 'flex', gap: 16 }}>
          <span><span style={{ color: v6.mute, fontSize: 9, letterSpacing: 1, marginRight: 5 }}>MIN</span>12.4°C</span>
          <span><span style={{ color: v6.mute, fontSize: 9, letterSpacing: 1, marginRight: 5 }}>AVG</span>23.8°C</span>
          <span style={{ color: '#ffd23a' }}><span style={{ color: v6.mute, fontSize: 9, letterSpacing: 1, marginRight: 5 }}>MAX</span>64.2°C</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <span style={{ color: v6.mute, fontSize: 9, letterSpacing: 1 }}>IRON</span>
          <div style={{ position: 'relative' }}>
            <div style={{ width: 260, height: 5, background: 'linear-gradient(90deg, #1a0a2e, #5a1a3a, #c43a1f, #ff8a3d, #ffd23a, #fff8d0)' }} />
            {/* A1: tick marks at min / avg / max */}
            <div style={{ position: 'absolute', top: 5, left: 0, right: 0, height: 12, pointerEvents: 'none' }}>
              {[
                { v: 12.4, label: '12' },
                { v: 23.8, label: '24' },
                { v: 64.2, label: '64' },
              ].map((t, i) => {
                const pct = (t.v / 78) * 100;
                return (
                  <div key={i} style={{ position: 'absolute', left: `${pct}%`, top: 0, transform: 'translateX(-50%)' }}>
                    <div style={{ width: 1, height: 4, background: '#fff', opacity: 0.7 }} />
                    <div style={{ ...v6.mono, fontSize: 8, color: v6.textDim, marginTop: 1, letterSpacing: 0.5 }}>{t.label}</div>
                  </div>
                );
              })}
            </div>
          </div>
          <span style={{ color: v6.mute, fontSize: 9, letterSpacing: 1 }}>78°C</span>
        </div>
      </div>

      {/* Position breadcrumb — bottom left, above FAB */}
      <div style={{
        position: 'absolute', bottom: 16, left: 16,
        background: 'rgba(6,7,10,0.6)',
        backdropFilter: 'blur(10px)', WebkitBackdropFilter: 'blur(10px)',
        border: `1px solid ${v6.hair}`,
        padding: '7px 12px',
        ...v6.mono, fontSize: 11, color: '#fff', letterSpacing: 0.5,
        zIndex: 4,
      }}>
        X 638  Y 472  Z 1280 mm
      </div>

      {/* REC indicator — bottom-right, balancing the position breadcrumb */}
      <div style={{
        position: 'absolute', bottom: 16, right: 16,
        background: 'rgba(6,7,10,0.6)',
        backdropFilter: 'blur(10px)', WebkitBackdropFilter: 'blur(10px)',
        border: `1px solid ${v6.hair}`,
        padding: '7px 12px',
        ...v6.mono, fontSize: 11, color: '#fff', letterSpacing: 0.5,
        zIndex: 4,
        display: 'flex', alignItems: 'center', gap: 10,
      }}>
        <div style={{ width: 8, height: 8, borderRadius: '50%', background: '#ff5a5a', boxShadow: '0 0 8px #ff5a5a', animation: 'pulse 1.2s infinite' }} />
        <span>REC · 14:22:08</span>
        <span style={{ color: v6.mute, fontSize: 9.5 }}>29.7 fps</span>
      </div>
    </div>
  );
}

// ─── A1 corner brackets (hairline overlay on the hero feed) ─────────
function V6FeedCorners() {
  const L = 14;
  const o = 14;
  const arms = [
    { x: o,                          y: o,                          d: 'M0 0 L14 0 M0 0 L0 14' },
    { x: `calc(100% - ${o + L}px)`,  y: o,                          d: 'M14 0 L0 0 M14 0 L14 14' },
    { x: o,                          y: `calc(100% - ${o + L}px)`,  d: 'M0 14 L0 0 M0 14 L14 14' },
    { x: `calc(100% - ${o + L}px)`,  y: `calc(100% - ${o + L}px)`,  d: 'M14 14 L0 14 M14 14 L14 0' },
  ];
  return (
    <div style={{ position: 'absolute', inset: 0, pointerEvents: 'none', zIndex: 5 }}>
      {arms.map((a, i) => (
        <svg key={i} width={L} height={L} style={{ position: 'absolute', left: a.x, top: a.y }} viewBox="0 0 14 14">
          <path d={a.d} stroke={v6.text} strokeWidth="1" fill="none" opacity="0.55" />
        </svg>
      ))}
    </div>
  );
}

// ─── Detection card (A1: cleaner — left accent border, no status pill) ──
function V6DetectionCard({ d, onClick }) {
  const pri = PRIORITY[d.priority];
  return (
    <div
      onClick={onClick}
      style={{
        flex: '0 0 172px',
        background: v6.inset,
        border: `1px solid ${v6.hair}`,
        borderLeft: `2px solid ${pri.color}`,
        padding: 8,
        display: 'flex', flexDirection: 'column', gap: 6,
        cursor: 'pointer',
        position: 'relative',
        transition: 'background 0.12s',
      }}
      onMouseEnter={(e) => { e.currentTarget.style.background = v6.surfaceHi; }}
      onMouseLeave={(e) => { e.currentTarget.style.background = v6.inset; }}
    >
      {/* Top: priority + temp */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
        <span style={{ ...v6.mono, fontSize: 8.5, letterSpacing: 1.5, color: pri.color, fontWeight: 700, textTransform: 'uppercase' }}>{pri.label}</span>
        <span style={{ ...v6.mono, fontSize: 10.5, color: '#fff', fontWeight: 500 }}>{d.temp}</span>
      </div>

      {/* Past + Current thumbs */}
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 3, flex: 1, minHeight: 0 }}>
        <div style={{ position: 'relative', overflow: 'hidden' }}>
          <HistoryThumb mode="raw" anomaly={false} />
          <div style={{ position: 'absolute', bottom: 2, left: 3, ...v6.mono, fontSize: 7, color: '#fff', textShadow: '0 1px 2px #000', letterSpacing: 1 }}>PAST</div>
        </div>
        <div style={{ position: 'relative', overflow: 'hidden' }}>
          <HistoryThumb mode="raw" anomaly={true} />
          <div style={{ position: 'absolute', bottom: 2, left: 3, ...v6.mono, fontSize: 7, color: '#fff', textShadow: '0 1px 2px #000', letterSpacing: 1 }}>CURRENT</div>
        </div>
      </div>

      {/* Area + time */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 6, minWidth: 0 }}>
        <span style={{ ...v6.mono, fontSize: 8.5, color: v6.textDim, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', flex: 1 }}>{d.area}</span>
        <span style={{ ...v6.mono, fontSize: 8.5, color: v6.faint, flex: '0 0 auto' }}>{d.time}</span>
      </div>
    </div>
  );
}

// ─── History strip — CRITICAL + HIGH only, with "+N more" chip ───────
function V6History({ onOpenDetail }) {
  const sorted = [...DETECTIONS].sort((a, b) => (b.date + ' ' + b.time).localeCompare(a.date + ' ' + a.time));
  const visible = sorted.filter(d => d.priority === 'critical' || d.priority === 'high');
  const today = sorted[0]?.date;
  const todayCount = sorted.filter(d => d.date === today).length;

  return (
    <div style={{
      flex: '0 0 180px',
      background: v6.surface,
      borderTop: `1px solid ${v6.hair}`,
      padding: '12px 22px 14px',
      display: 'flex', flexDirection: 'column', gap: 8,
      overflow: 'hidden',
    }}>
      <div style={{ display: 'flex', alignItems: 'baseline', gap: 14 }}>
        <span style={{ ...v6.mono, fontSize: 9.5, letterSpacing: 2.5, color: v6.mute, textTransform: 'uppercase' }}>Detections</span>
        <span style={{ fontFamily: '"Helvetica Neue", Helvetica, Arial, sans-serif', fontSize: 14, color: v6.text, fontWeight: 500, letterSpacing: 0.5 }}>
          {todayCount} LEAKS TODAY
        </span>
        <span style={{ ...v6.mono, fontSize: 9, color: v6.faint, letterSpacing: 1.5 }}>· SHOWING CRITICAL + HIGH</span>
        <div style={{ flex: 1 }} />
        <button onClick={() => onOpenDetail(null)} style={{
          background: 'transparent', border: `1px solid ${v6.hairBright}`,
          color: v6.text, padding: '5px 12px',
          ...v6.mono, fontSize: 9.5, letterSpacing: 1.5, cursor: 'pointer',
          display: 'flex', alignItems: 'center', gap: 6,
        }}
        onMouseEnter={(e) => { e.currentTarget.style.borderColor = OMV_BLUE_GLOW; e.currentTarget.style.color = '#fff'; }}
        onMouseLeave={(e) => { e.currentTarget.style.borderColor = v6.hairBright; e.currentTarget.style.color = v6.text; }}>
          VIEW ALL · {DETECTIONS.length}
          <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M5 12h14 M12 5l7 7-7 7" /></svg>
        </button>
      </div>
      <div style={{ position: 'relative', overflowX: 'auto', overflowY: 'hidden', flex: 1, minHeight: 0 }}>
        <div style={{ display: 'flex', gap: 10, height: '100%' }}>
          {visible.map(d => (
            <V6DetectionCard key={d.id} d={d} onClick={() => onOpenDetail(d)} />
          ))}
        </div>
        <div style={{ position: 'absolute', top: 0, right: 0, bottom: 0, width: 50, background: `linear-gradient(90deg, transparent, ${v6.surface})`, pointerEvents: 'none' }} />
      </div>
    </div>
  );
}

// ─── Alarm modal ─────────────────────────────────────────────────────
function V6AlarmModal({ detection, onAcknowledge, onSwitchManual }) {
  const pri = PRIORITY[detection.priority];
  const isCritical = detection.priority === 'critical';
  return (
    <div style={{
      position: 'absolute', inset: 0, zIndex: 100,
      background: 'rgba(0,0,0,0.62)',
      backdropFilter: 'blur(6px)', WebkitBackdropFilter: 'blur(6px)',
      display: 'grid', placeItems: 'center',
    }}>
      <div style={{
        width: 560,
        background: v6.surface,
        border: `1.5px solid ${pri.color}`,
        boxShadow: `0 30px 80px rgba(0,0,0,0.85), 0 0 0 6px ${pri.glow}`,
      }}>
        {/* Header */}
        <div style={{
          padding: '16px 22px',
          background: `linear-gradient(180deg, ${pri.color}22, transparent)`,
          borderBottom: `1px solid ${pri.color}66`,
          display: 'flex', alignItems: 'center', justifyContent: 'space-between',
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
            <div style={{
              width: 12, height: 12, borderRadius: '50%',
              background: pri.color,
              boxShadow: `0 0 12px ${pri.color}`,
              animation: 'pulse 1s infinite',
            }} />
            <div>
              <div style={{ ...v6.mono, fontSize: 11, letterSpacing: 3, color: pri.color, fontWeight: 700 }}>
                {pri.label} · LEAK DETECTED
              </div>
              <div style={{ ...v6.mono, fontSize: 9.5, letterSpacing: 1.5, color: v6.mute, marginTop: 4 }}>
                {detection.id}  ·  PASS {detection.pass}  ·  {detection.time}
              </div>
            </div>
          </div>
          {!isCritical && (
            <button onClick={onAcknowledge} style={{
              background: 'transparent', border: 'none', color: v6.mute,
              cursor: 'pointer', padding: 6,
              display: 'grid', placeItems: 'center',
            }}>
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M6 6 L18 18 M18 6 L6 18" />
              </svg>
            </button>
          )}
        </div>

        {/* Body */}
        <div style={{ padding: 22, display: 'flex', gap: 22 }}>
          <div style={{ width: 200, height: 150, background: '#000', flex: '0 0 200px', position: 'relative', overflow: 'hidden' }}>
            <HistoryThumb mode="raw" anomaly={true} w={200} h={150} />
            <div style={{ position: 'absolute', top: 8, left: 8, ...v6.mono, fontSize: 8.5, letterSpacing: 1.5, color: pri.color, background: 'rgba(0,0,0,0.5)', padding: '3px 7px', fontWeight: 700 }}>HOTSPOT</div>
          </div>
          <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 14 }}>
            <div>
              <div style={{ ...v6.mono, fontSize: 9, color: v6.faint, letterSpacing: 1.5 }}>TEMPERATURE</div>
              <div style={{ display: 'flex', alignItems: 'baseline', gap: 8, marginTop: 4 }}>
                <span style={{ fontFamily: '"Helvetica Neue", sans-serif', fontWeight: 200, fontSize: 40, color: '#fff', lineHeight: 1, letterSpacing: -1.5 }}>{detection.tempVal.toFixed(1)}</span>
                <span style={{ ...v6.mono, fontSize: 13, color: v6.mute }}>°C</span>
                <span style={{ ...v6.mono, fontSize: 13, color: pri.color, marginLeft: 10, fontWeight: 600 }}>{detection.delta}</span>
              </div>
            </div>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
              {[
                ['LOCATION', `X ${detection.location.x}  Y ${detection.location.y}`],
                ['CONFIDENCE', `${Math.round(detection.confidence * 100)}%`],
                ['AREA', detection.area],
                ['STATUS', 'NEW'],
              ].map(([k, v]) => (
                <div key={k}>
                  <div style={{ ...v6.mono, fontSize: 8.5, color: v6.faint, letterSpacing: 1.5 }}>{k}</div>
                  <div style={{ ...v6.mono, fontSize: 11.5, color: v6.text, marginTop: 3 }}>{v}</div>
                </div>
              ))}
            </div>
          </div>
        </div>

        {/* Pause banner */}
        <div style={{
          padding: '10px 22px',
          background: 'rgba(229,185,74,0.10)',
          borderTop: `1px solid rgba(229,185,74,0.3)`,
          ...v6.mono, fontSize: 10, letterSpacing: 1.4, color: v6.warn,
          display: 'flex', alignItems: 'center', gap: 8,
        }}>
          <IconPause size={11} /> AUTONOMOUS SCAN PAUSED · AWAITING ACKNOWLEDGEMENT
        </div>

        {/* Actions */}
        <div style={{ padding: '14px 22px', display: 'flex', gap: 10, borderTop: `1px solid ${v6.hair}` }}>
          {isCritical ? (
            <>
              <button onClick={onAcknowledge} style={{
                flex: 1, padding: '12px 16px',
                background: 'rgba(255,255,255,0.05)',
                border: `1px solid ${v6.hairBright}`,
                color: v6.text, cursor: 'pointer',
                ...v6.mono, fontSize: 11, letterSpacing: 2, fontWeight: 600,
              }}>ACKNOWLEDGE</button>
              <button onClick={onSwitchManual} style={{
                flex: 1, padding: '12px 16px',
                background: OMV_NAVY_HI,
                border: `1px solid ${OMV_BLUE_GLOW}`,
                color: '#fff', cursor: 'pointer',
                ...v6.mono, fontSize: 11, letterSpacing: 2, fontWeight: 700,
                display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
              }}>
                <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <path d="M12 2 L12 22 M2 12 L22 12" /><circle cx="12" cy="12" r="2" fill="currentColor" />
                </svg>
                SWITCH TO MANUAL
              </button>
            </>
          ) : (
            <button onClick={onAcknowledge} style={{
              flex: 1, padding: '12px 16px',
              background: OMV_NAVY,
              border: `1px solid ${OMV_NAVY_HI}`,
              color: '#fff', cursor: 'pointer',
              ...v6.mono, fontSize: 11, letterSpacing: 2, fontWeight: 600,
            }}>MARK AS SEEN</button>
          )}
        </div>
      </div>
    </div>
  );
}

// ─── Root ─────────────────────────────────────────────────────────────
function V6Argus() {
  const [activeTab, setActiveTab] = React.useState('inspection');
  const [running, setRunning] = React.useState(true);
  const [alarm, setAlarm] = React.useState(null);
  const [showDetail, setShowDetail] = React.useState(false);
  const [selectedDetection, setSelectedDetection] = React.useState(null);
  const isManual = activeTab === 'manual';

  // Demo: fire alarm 2.5s after initial mount so spectators see the modal
  React.useEffect(() => {
    const t = setTimeout(() => setAlarm(DETECTIONS[0]), 2500);
    return () => clearTimeout(t);
  }, []);

  const HistoryDetailPage = window.V6HistoryDetailPage;

  return (
    <div style={{
      width: 1440, height: 900,
      background: v6.canvas,
      color: v6.text,
      fontFamily: '"Helvetica Neue", Helvetica, Arial, sans-serif',
      display: 'flex', flexDirection: 'column',
      overflow: 'hidden',
      position: 'relative',
    }} data-screen-label="01 Argus">
      <V6Header />
      <div style={{ flex: 1, display: 'flex', minHeight: 0, position: 'relative' }}>
        <V6HeroFeed />
        <V6RightRail
          activeTab={activeTab}
          onTabChange={setActiveTab}
          running={running && !isManual && !alarm}
          onPauseResume={() => setRunning(!running)}
        />
      </div>
      <V6History onOpenDetail={(d) => { setSelectedDetection(d); setShowDetail(true); }} />

      {alarm && (
        <V6AlarmModal
          detection={alarm}
          onAcknowledge={() => setAlarm(null)}
          onSwitchManual={() => { setAlarm(null); setActiveTab('manual'); }}
        />
      )}

      {showDetail && HistoryDetailPage && (
        <HistoryDetailPage
          initialSelected={selectedDetection}
          onClose={() => { setShowDetail(false); setSelectedDetection(null); }}
        />
      )}
    </div>
  );
}

window.V6Argus = V6Argus;
Object.assign(window, { v6, OMV_NAVY, OMV_NAVY_DEEP, OMV_NAVY_HI, OMV_BLUE_GLOW });
