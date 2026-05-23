// Shared data + priority/status metadata for V6 Argus.
// Exposes via window so v6-argus.jsx and v6-history-detail.jsx can both read.

const PRIORITY = {
  critical: { label: 'CRITICAL', color: '#e54545', glow: 'rgba(229,69,69,0.4)', rank: 0 },
  high:     { label: 'HIGH',     color: '#ff8a3d', glow: 'rgba(255,138,61,0.4)', rank: 1 },
  medium:   { label: 'MEDIUM',   color: '#e5b94a', glow: 'rgba(229,185,74,0.3)', rank: 2 },
  low:      { label: 'LOW',      color: '#7d96b8', glow: 'rgba(125,150,184,0.3)', rank: 3 },
};

const STATUS = {
  new:            { label: 'NEW',             color: '#e54545' },
  acknowledged:   { label: 'ACKNOWLEDGED',    color: '#3b8fe0' },
  resolved:       { label: 'RESOLVED',        color: '#5bba6f' },
  false_positive: { label: 'FALSE POSITIVE',  color: '#7a7a7a' },
};

// Live detection records arrive from the Pi backend over the SocketIO
// "new_detection" event and are held in V6Argus's React state — this array
// stays empty. PRIORITY and STATUS above are static config (color/label maps),
// not demo data, so they remain. The detection schema is documented in
// Argus Spec §5 and is the contract the backend must satisfy.
const DETECTIONS = [];

Object.assign(window, { PRIORITY, STATUS, DETECTIONS });
