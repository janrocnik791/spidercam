// Shared design tokens, thermal feed, icons, helpers for all 3 variations.

const OMV_BLUE = '#0057A8';
const OMV_BLUE_DIM = '#0a3a6e';
const OMV_BLUE_BRIGHT = '#3b8fe0';

// Iron-palette thermal feed. Pretends to be a 640×480 IR sensor feed with
// pipework / wall structure and one hotspot anomaly in the lower-left.
// Variants: 'thermal' (iron palette) or 'normal' (grayscale visible camera).
function ThermalFeed({ mode = 'thermal', anomaly = true, w = 800, h = 600, showCrosshair = true, showBrackets = true, label = 'IR · 640×480' }) {
  const id = React.useId().replace(/:/g, '');
  return (
    <svg viewBox={`0 0 ${w} ${h}`} width="100%" height="100%" preserveAspectRatio="xMidYMid slice" style={{ display: 'block', background: '#000' }}>
      <defs>
        {/* Iron palette background — vertical gradient cool→warm */}
        <radialGradient id={`bg-${id}`} cx="50%" cy="50%" r="80%">
          <stop offset="0%" stopColor={mode === 'thermal' ? '#3a1f5e' : '#3a3a3a'} />
          <stop offset="60%" stopColor={mode === 'thermal' ? '#1a0a2e' : '#1a1a1a'} />
          <stop offset="100%" stopColor={mode === 'thermal' ? '#08051a' : '#0a0a0a'} />
        </radialGradient>
        {/* Warm structure (mid temp) */}
        <radialGradient id={`warm-${id}`}>
          <stop offset="0%" stopColor={mode === 'thermal' ? '#ff8a3d' : '#9a9a9a'} stopOpacity="0.95" />
          <stop offset="50%" stopColor={mode === 'thermal' ? '#c43a1f' : '#5a5a5a'} stopOpacity="0.6" />
          <stop offset="100%" stopColor={mode === 'thermal' ? '#5a1530' : '#2a2a2a'} stopOpacity="0" />
        </radialGradient>
        {/* Hot anomaly */}
        <radialGradient id={`hot-${id}`}>
          <stop offset="0%" stopColor={mode === 'thermal' ? '#fff8d0' : '#e0e0e0'} stopOpacity="1" />
          <stop offset="25%" stopColor={mode === 'thermal' ? '#ffd23a' : '#b0b0b0'} stopOpacity="0.95" />
          <stop offset="55%" stopColor={mode === 'thermal' ? '#ff5a1a' : '#707070'} stopOpacity="0.7" />
          <stop offset="100%" stopColor={mode === 'thermal' ? '#7a1010' : '#2a2a2a'} stopOpacity="0" />
        </radialGradient>
        {/* Pipe-line linear */}
        <linearGradient id={`pipe-${id}`} x1="0" y1="0" x2="1" y2="0">
          <stop offset="0%" stopColor={mode === 'thermal' ? '#7a2e5a' : '#3a3a3a'} />
          <stop offset="50%" stopColor={mode === 'thermal' ? '#c4582a' : '#6a6a6a'} />
          <stop offset="100%" stopColor={mode === 'thermal' ? '#5a1a3a' : '#2a2a2a'} />
        </linearGradient>
        <filter id={`blur-${id}`}><feGaussianBlur stdDeviation="8" /></filter>
        <filter id={`blurS-${id}`}><feGaussianBlur stdDeviation="3" /></filter>
      </defs>
      <rect width={w} height={h} fill={`url(#bg-${id})`} />
      {/* Simulated structure: horizontal pipework + vertical riser */}
      <g filter={`url(#blur-${id})`} opacity="0.85">
        <rect x={w * 0.05} y={h * 0.28} width={w * 0.9} height={h * 0.10} fill={`url(#pipe-${id})`} />
        <rect x={w * 0.62} y={h * 0.10} width={w * 0.08} height={h * 0.75} fill={`url(#pipe-${id})`} opacity="0.7" />
        <ellipse cx={w * 0.25} cy={h * 0.65} rx={w * 0.22} ry={h * 0.18} fill={`url(#warm-${id})`} />
        <ellipse cx={w * 0.78} cy={h * 0.78} rx={w * 0.18} ry={h * 0.14} fill={`url(#warm-${id})`} opacity="0.6" />
      </g>
      {/* Hot anomaly blob */}
      {anomaly && (
        <ellipse cx={w * 0.32} cy={h * 0.72} rx={w * 0.09} ry={h * 0.10} fill={`url(#hot-${id})`} filter={`url(#blurS-${id})`} />
      )}
      {/* Subtle scan-line texture */}
      <g opacity="0.08">
        {Array.from({ length: 60 }).map((_, i) => (
          <line key={i} x1="0" x2={w} y1={i * (h / 60)} y2={i * (h / 60)} stroke="#fff" strokeWidth="0.5" />
        ))}
      </g>
      {/* Anomaly box */}
      {anomaly && (
        <g>
          <rect x={w * 0.21} y={h * 0.60} width={w * 0.22} height={h * 0.24} fill="none" stroke="#ffd23a" strokeWidth="2" strokeDasharray="6 4" />
          <text x={w * 0.21} y={h * 0.59} fontFamily="'JetBrains Mono', monospace" fontSize="14" fill="#ffd23a" fontWeight="700">ANOMALY · 64.2°C</text>
        </g>
      )}
      {/* Corner brackets */}
      {showBrackets && (
        <g stroke="#fff" strokeWidth="2" fill="none" opacity="0.55">
          <path d={`M 16 36 L 16 16 L 36 16`} />
          <path d={`M ${w - 36} 16 L ${w - 16} 16 L ${w - 16} 36`} />
          <path d={`M 16 ${h - 36} L 16 ${h - 16} L 36 ${h - 16}`} />
          <path d={`M ${w - 36} ${h - 16} L ${w - 16} ${h - 16} L ${w - 16} ${h - 36}`} />
        </g>
      )}
      {/* Center crosshair */}
      {showCrosshair && (
        <g stroke="#fff" strokeWidth="1" opacity="0.45">
          <line x1={w / 2 - 18} y1={h / 2} x2={w / 2 - 4} y2={h / 2} />
          <line x1={w / 2 + 4} y1={h / 2} x2={w / 2 + 18} y2={h / 2} />
          <line x1={w / 2} y1={h / 2 - 18} x2={w / 2} y2={h / 2 - 4} />
          <line x1={w / 2} y1={h / 2 + 4} x2={w / 2} y2={h / 2 + 18} />
          <circle cx={w / 2} cy={h / 2} r="1.5" fill="#fff" />
        </g>
      )}
      {/* Top-left label (rendered only when explicitly provided; null skips). */}
      {label && (
        <g fontFamily="'JetBrains Mono', monospace" fill="#fff" fontSize="11">
          <text x="28" y="60" opacity="0.85">{label}</text>
          <text x="28" y="76" opacity="0.55">{mode === 'thermal' ? 'IRON · 12–78°C' : 'VISIBLE · RGB'}</text>
        </g>
      )}
      {/* REC + fps now live in the React HUD layer so they can be positioned
          relative to the other overlays (anomaly banner, temp tape, etc). */}
    </svg>
  );
}

// Coverage map: top-down rectangle showing scan area, planned serpentine
// path, completed cells, current head position.
function CoverageMap({ progress = 0.42, cols = 12, rows = 8, accent = OMV_BLUE_BRIGHT, w = 360, h = 240 }) {
  const cellW = w / cols, cellH = h / rows;
  // Serpentine fill: rows alternate direction
  const total = cols * rows;
  const done = Math.floor(total * progress);
  const cells = [];
  let idx = 0;
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const col = r % 2 === 0 ? c : cols - 1 - c;
      cells.push({ x: col * cellW, y: r * cellH, done: idx < done, current: idx === done });
      idx++;
    }
  }
  return (
    <svg viewBox={`0 0 ${w} ${h}`} width="100%" height="100%" style={{ display: 'block' }}>
      <rect width={w} height={h} fill="#0a0d12" />
      {/* Grid */}
      <g stroke="rgba(255,255,255,0.04)" strokeWidth="0.5">
        {Array.from({ length: cols + 1 }).map((_, i) => (
          <line key={`v${i}`} x1={i * cellW} y1="0" x2={i * cellW} y2={h} />
        ))}
        {Array.from({ length: rows + 1 }).map((_, i) => (
          <line key={`h${i}`} x1="0" y1={i * cellH} x2={w} y2={i * cellH} />
        ))}
      </g>
      {/* Completed cells */}
      {cells.filter(c => c.done).map((c, i) => (
        <rect key={i} x={c.x + 1} y={c.y + 1} width={cellW - 2} height={cellH - 2} fill={accent} opacity="0.18" />
      ))}
      {/* Current cell */}
      {cells.filter(c => c.current).map((c, i) => (
        <rect key={i} x={c.x + 1} y={c.y + 1} width={cellW - 2} height={cellH - 2} fill={accent} opacity="0.85" />
      ))}
      {/* Border */}
      <rect x="0.5" y="0.5" width={w - 1} height={h - 1} fill="none" stroke="rgba(255,255,255,0.18)" />
      {/* Origin marker */}
      <g fontFamily="'JetBrains Mono', monospace" fill="rgba(255,255,255,0.45)" fontSize="9">
        <text x="6" y="14">0,0</text>
        <text x={w - 6} y={h - 6} textAnchor="end">{cols * 100}mm × {rows * 100}mm</text>
      </g>
    </svg>
  );
}

// History thumbnail — small thermal thumbnail with optional anomaly highlight.
function HistoryThumb({ mode = 'diff', anomaly = false, w = 180, h = 110 }) {
  const id = React.useId().replace(/:/g, '');
  return (
    <svg viewBox={`0 0 ${w} ${h}`} width="100%" height="100%" preserveAspectRatio="xMidYMid slice" style={{ display: 'block', background: '#000' }}>
      <defs>
        <radialGradient id={`bg-${id}`} cx="50%" cy="50%" r="80%">
          <stop offset="0%" stopColor={mode === 'diff' ? '#0a1430' : '#3a1f5e'} />
          <stop offset="100%" stopColor="#06040d" />
        </radialGradient>
        <radialGradient id={`spot-${id}`}>
          <stop offset="0%" stopColor={mode === 'diff' ? '#ff3a3a' : '#ffd23a'} stopOpacity={anomaly ? 1 : 0.4} />
          <stop offset="60%" stopColor={mode === 'diff' ? '#7a1010' : '#c4582a'} stopOpacity="0.5" />
          <stop offset="100%" stopOpacity="0" />
        </radialGradient>
        <linearGradient id={`pipe-${id}`}>
          <stop offset="0%" stopColor={mode === 'diff' ? '#1a3a6a' : '#7a2e5a'} stopOpacity="0.6" />
          <stop offset="100%" stopColor={mode === 'diff' ? '#0a1a3a' : '#5a1a3a'} stopOpacity="0.3" />
        </linearGradient>
      </defs>
      <rect width={w} height={h} fill={`url(#bg-${id})`} />
      {mode === 'raw' && <rect x={w * 0.1} y={h * 0.3} width={w * 0.8} height={h * 0.18} fill={`url(#pipe-${id})`} />}
      {mode === 'diff' && (
        <g opacity="0.7">
          <rect x={w * 0.1} y={h * 0.3} width={w * 0.8} height={h * 0.18} fill={`url(#pipe-${id})`} />
        </g>
      )}
      <ellipse cx={w * (anomaly ? 0.32 : 0.55)} cy={h * 0.65} rx={w * 0.12} ry={h * 0.18} fill={`url(#spot-${id})`} />
      {anomaly && mode === 'diff' && (
        <rect x={w * 0.22} y={h * 0.5} width={w * 0.22} height={h * 0.32} fill="none" stroke="#ff3a3a" strokeWidth="1" strokeDasharray="3 2" />
      )}
    </svg>
  );
}

// ── Icons ─────────────────────────────────────────────────────────────
const Icon = ({ d, size = 18, stroke = 1.6 }) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={stroke} strokeLinecap="round" strokeLinejoin="round">
    {typeof d === 'string' ? <path d={d} /> : d}
  </svg>
);
const IconArrowUp = (p) => <Icon {...p} d="M12 19V5 M5 12l7-7 7 7" />;
const IconArrowDown = (p) => <Icon {...p} d="M12 5v14 M19 12l-7 7-7-7" />;
const IconArrowLeft = (p) => <Icon {...p} d="M19 12H5 M12 19l-7-7 7-7" />;
const IconArrowRight = (p) => <Icon {...p} d="M5 12h14 M12 5l7 7-7 7" />;
const IconHome = (p) => <Icon {...p} d={<><path d="M3 11l9-8 9 8" /><path d="M5 10v10h14V10" /></>} />;
const IconPlay = (p) => <Icon {...p} d="M6 4l14 8-14 8V4z" />;
const IconPause = (p) => <Icon {...p} d={<><rect x="6" y="5" width="4" height="14" /><rect x="14" y="5" width="4" height="14" /></>} />;
const IconStop = (p) => <Icon {...p} d={<rect x="6" y="6" width="12" height="12" />} />;
const IconCamera = (p) => <Icon {...p} d={<><path d="M3 7h4l2-3h6l2 3h4v13H3z" /><circle cx="12" cy="13" r="4" /></>} />;
const IconThermal = (p) => <Icon {...p} d={<><path d="M14 14V5a2 2 0 0 0-4 0v9a4 4 0 1 0 4 0z" /><line x1="12" y1="9" x2="12" y2="14" /></>} />;
const IconWifi = (p) => <Icon {...p} d="M5 12a10 10 0 0 1 14 0 M8.5 15.5a5 5 0 0 1 7 0 M12 19v.01" />;
const IconAlert = (p) => <Icon {...p} d={<><path d="M12 2L2 21h20L12 2z" /><line x1="12" y1="10" x2="12" y2="15" /><circle cx="12" cy="18" r="0.5" fill="currentColor" /></>} />;
const IconSwap = (p) => <Icon {...p} d="M7 7h13l-3-3 M17 17H4l3 3" />;
const IconLayers = (p) => <Icon {...p} d={<><polygon points="12 2 2 7 12 12 22 7 12 2" /><polyline points="2 17 12 22 22 17" /><polyline points="2 12 12 17 22 12" /></>} />;
const IconChip = (p) => <Icon {...p} d={<><rect x="6" y="6" width="12" height="12" rx="1" /><path d="M9 1v4 M15 1v4 M9 19v4 M15 19v4 M1 9h4 M1 15h4 M19 9h4 M19 15h4" /></>} />;
const IconExpand = (p) => <Icon {...p} d="M3 9V3h6 M21 9V3h-6 M3 15v6h6 M21 15v6h-6" />;

Object.assign(window, {
  OMV_BLUE, OMV_BLUE_DIM, OMV_BLUE_BRIGHT,
  ThermalFeed, CoverageMap, HistoryThumb,
  IconArrowUp, IconArrowDown, IconArrowLeft, IconArrowRight,
  IconHome, IconPlay, IconPause, IconStop, IconCamera, IconThermal,
  IconWifi, IconAlert, IconSwap, IconLayers, IconChip, IconExpand,
});
