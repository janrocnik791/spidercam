// V6 — History Detail Page (full-overlay)
// Slides over V6Argus. Filters by priority, status, date, mission, area.
// List on left, full detail on right.

(() => {
  const { v6, OMV_NAVY, OMV_NAVY_HI, OMV_BLUE_GLOW, DETECTIONS, PRIORITY, STATUS } = window;

  function PriorityFilterPill({ k, label, active, count, onClick }) {
    const color = k === 'all' ? OMV_BLUE_GLOW : PRIORITY[k]?.color || v6.mute;
    return (
      <button onClick={onClick} style={{
        padding: '6px 12px',
        background: active ? `${color}22` : 'transparent',
        border: `1px solid ${active ? color : v6.hair}`,
        color: active ? '#fff' : v6.textDim,
        cursor: 'pointer',
        ...v6.mono, fontSize: 10, letterSpacing: 1.5, fontWeight: active ? 600 : 400,
        display: 'flex', alignItems: 'center', gap: 6,
        transition: 'background 0.12s, border-color 0.12s, color 0.12s',
      }}>
        {k !== 'all' && <span style={{ width: 6, height: 6, borderRadius: '50%', background: color }} />}
        {label}
        {count !== undefined && (
          <span style={{ opacity: 0.55, marginLeft: 4 }}>· {count}</span>
        )}
      </button>
    );
  }

  function StatusFilterPill({ k, label, active, onClick }) {
    const color = k === 'all' ? OMV_BLUE_GLOW : STATUS[k]?.color || v6.mute;
    return (
      <button onClick={onClick} style={{
        padding: '5px 10px',
        background: active ? `${color}22` : 'transparent',
        border: `1px solid ${active ? color : v6.hair}`,
        color: active ? '#fff' : v6.textDim,
        cursor: 'pointer',
        ...v6.mono, fontSize: 9.5, letterSpacing: 1.4, fontWeight: active ? 600 : 400,
        transition: 'background 0.12s, border-color 0.12s, color 0.12s',
      }}>
        {label}
      </button>
    );
  }

  function LocationPin({ detection, w = 200, h = 132 }) {
    const margin = 6;
    const cx = margin + detection.locNorm.x * (w - margin * 2);
    const cy = margin + detection.locNorm.y * (h - margin * 2);
    const color = PRIORITY[detection.priority].color;
    return (
      <svg viewBox={`0 0 ${w} ${h}`} width="100%" height="100%" style={{ display: 'block' }}>
        <rect width={w} height={h} fill={v6.inset} />
        <rect x={margin} y={margin} width={w - margin * 2} height={h - margin * 2} fill="none" stroke="rgba(255,255,255,0.10)" strokeDasharray="2 3" />
        {/* faint sector grid */}
        <g stroke="rgba(255,255,255,0.04)" strokeWidth="0.5">
          <line x1={(w - margin * 2) / 2 + margin} y1={margin} x2={(w - margin * 2) / 2 + margin} y2={h - margin} />
          <line x1={margin} y1={(h - margin * 2) / 2 + margin} x2={w - margin} y2={(h - margin * 2) / 2 + margin} />
        </g>
        {/* other detections, dim */}
        {DETECTIONS.filter(d => d.id !== detection.id).map(d => (
          <circle key={d.id} cx={margin + d.locNorm.x * (w - margin * 2)} cy={margin + d.locNorm.y * (h - margin * 2)} r="2" fill={PRIORITY[d.priority].color} opacity="0.25" />
        ))}
        {/* target */}
        <circle cx={cx} cy={cy} r="10" fill={`${color}33`}>
          <animate attributeName="r" values="8;12;8" dur="1.8s" repeatCount="indefinite" />
        </circle>
        <circle cx={cx} cy={cy} r="4" fill={color} stroke="#fff" strokeWidth="1.3" />
        <g fontFamily="'JetBrains Mono', monospace" fill="rgba(255,255,255,0.45)" fontSize="8">
          <text x={margin} y={margin - 1}>0,0</text>
          <text x={w - margin} y={h - margin + 9} textAnchor="end">1200 × 800 mm</text>
        </g>
      </svg>
    );
  }

  function DetectionListItem({ d, selected, onClick }) {
    const pri = PRIORITY[d.priority];
    const sta = STATUS[d.status];
    return (
      <div
        onClick={onClick}
        style={{
          padding: '12px 16px',
          background: selected ? `${OMV_NAVY_HI}22` : 'transparent',
          borderLeft: `2px solid ${selected ? OMV_BLUE_GLOW : pri.color}`,
          borderBottom: `1px solid ${v6.hair}`,
          cursor: 'pointer',
          transition: 'background 0.12s',
          display: 'flex', alignItems: 'center', gap: 12,
        }}
        onMouseEnter={(e) => { if (!selected) e.currentTarget.style.background = 'rgba(255,255,255,0.025)'; }}
        onMouseLeave={(e) => { if (!selected) e.currentTarget.style.background = 'transparent'; }}
      >
        {/* Priority chip */}
        <div style={{ flex: '0 0 auto' }}>
          <div style={{ width: 8, height: 8, borderRadius: '50%', background: pri.color, boxShadow: `0 0 6px ${pri.color}` }} />
        </div>
        {/* Main info */}
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 8, marginBottom: 3 }}>
            <span style={{ ...v6.mono, fontSize: 11, color: '#fff', fontWeight: 500, letterSpacing: 0.3 }}>{d.id}</span>
            <span style={{ ...v6.mono, fontSize: 9, color: pri.color, letterSpacing: 1.5, fontWeight: 600 }}>{pri.label}</span>
          </div>
          <div style={{ ...v6.mono, fontSize: 10, color: v6.textDim, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
            {d.area}
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginTop: 4 }}>
            <span style={{ ...v6.mono, fontSize: 9, color: v6.faint, letterSpacing: 1 }}>{d.date} · {d.time}</span>
            <span style={{
              ...v6.mono, fontSize: 8, letterSpacing: 1, fontWeight: 600,
              color: sta.color, background: `${sta.color}22`,
              padding: '1px 5px',
            }}>{sta.label}</span>
          </div>
        </div>
        {/* Temp */}
        <div style={{ flex: '0 0 auto', textAlign: 'right' }}>
          <div style={{ ...v6.mono, fontSize: 14, color: '#fff', fontWeight: 500 }}>{d.temp}</div>
          <div style={{ ...v6.mono, fontSize: 9, color: pri.color, marginTop: 2 }}>{d.delta}</div>
        </div>
      </div>
    );
  }

  function DetectionDetail({ d, onSwitchManual }) {
    if (!d) {
      return (
        <div style={{ flex: 1, display: 'grid', placeItems: 'center', color: v6.faint, ...v6.mono, fontSize: 11, letterSpacing: 1.5 }}>
          SELECT A DETECTION FROM THE LIST
        </div>
      );
    }
    const pri = PRIORITY[d.priority];
    const sta = STATUS[d.status];
    return (
      <div style={{ flex: 1, padding: '24px 28px', display: 'flex', flexDirection: 'column', gap: 22, overflowY: 'auto' }}>
        {/* Header */}
        <div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 6 }}>
            <span style={{ width: 10, height: 10, borderRadius: '50%', background: pri.color, boxShadow: `0 0 10px ${pri.color}` }} />
            <span style={{ ...v6.mono, fontSize: 11, letterSpacing: 3, color: pri.color, fontWeight: 700 }}>{pri.label} · LEAK</span>
            <span style={{
              ...v6.mono, fontSize: 9.5, letterSpacing: 1.4, fontWeight: 600,
              color: sta.color, background: `${sta.color}22`,
              padding: '3px 8px',
            }}>{sta.label}</span>
          </div>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 12 }}>
            <div style={{ fontFamily: '"Helvetica Neue", Helvetica, Arial, sans-serif', fontWeight: 200, fontSize: 30, color: '#fff', lineHeight: 1, letterSpacing: 2 }}>{d.id}</div>
            <div style={{ ...v6.mono, fontSize: 11, color: v6.mute, letterSpacing: 1 }}>{d.date} · {d.time}</div>
          </div>
        </div>

        {/* Frames row */}
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 12 }}>
          {[
            { label: 'PAST FRAME', mode: 'raw', anomaly: false, note: `Pass ${(parseInt(d.pass) - 1).toString().padStart(3, '0')}` },
            { label: 'CURRENT FRAME', mode: 'raw', anomaly: true, note: `Pass ${d.pass}` },
            { label: 'DIFFERENCE', mode: 'diff', anomaly: true, note: 'Δ overlay' },
          ].map((f, i) => (
            <div key={i}>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 5 }}>
                <span style={{ ...v6.mono, fontSize: 9, color: v6.mute, letterSpacing: 1.5 }}>{f.label}</span>
                <span style={{ ...v6.mono, fontSize: 9, color: v6.faint }}>{f.note}</span>
              </div>
              <div style={{ aspectRatio: '4/3', background: '#000', position: 'relative', overflow: 'hidden' }}>
                <HistoryThumb mode={f.mode} anomaly={f.anomaly} />
              </div>
            </div>
          ))}
        </div>

        {/* Metadata grid */}
        <div>
          <div style={{ ...v6.mono, fontSize: 9, color: v6.faint, letterSpacing: 1.5, marginBottom: 10, textTransform: 'uppercase' }}>Details</div>
          <div style={{
            display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)',
            border: `1px solid ${v6.hair}`,
          }}>
            {[
              ['TEMPERATURE', `${d.tempVal.toFixed(1)}°C`, '#fff'],
              ['DELTA', d.delta, pri.color],
              ['CONFIDENCE', `${Math.round(d.confidence * 100)}%`, '#fff'],
              ['STATUS', sta.label, sta.color],
              ['LOCATION X', `${d.location.x} mm`, '#fff'],
              ['LOCATION Y', `${d.location.y} mm`, '#fff'],
              ['LOCATION Z', `${d.location.z} mm`, '#fff'],
              ['PASS', d.pass, '#fff'],
              ['AREA', d.area, '#fff'],
              ['MISSION', d.mission, '#fff'],
              ['DATE', d.date, '#fff'],
              ['TIME', d.time, '#fff'],
            ].map(([k, val, c], i) => (
              <div key={k} style={{
                padding: '10px 14px',
                borderLeft: i % 4 !== 0 ? `1px solid ${v6.hair}` : 'none',
                borderTop: i >= 4 ? `1px solid ${v6.hair}` : 'none',
              }}>
                <div style={{ ...v6.mono, fontSize: 8.5, color: v6.faint, letterSpacing: 1.5 }}>{k}</div>
                <div style={{ ...v6.mono, fontSize: 12, color: c, marginTop: 3, fontWeight: 500 }}>{val}</div>
              </div>
            ))}
          </div>
        </div>

        {/* Location on map + Notes */}
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 18 }}>
          <div>
            <div style={{ ...v6.mono, fontSize: 9, color: v6.faint, letterSpacing: 1.5, marginBottom: 10, textTransform: 'uppercase' }}>Location on plant</div>
            <div style={{ aspectRatio: '12/8', background: v6.inset }}>
              <LocationPin detection={d} />
            </div>
          </div>
          <div>
            <div style={{ ...v6.mono, fontSize: 9, color: v6.faint, letterSpacing: 1.5, marginBottom: 10, textTransform: 'uppercase' }}>Notes</div>
            <div style={{
              minHeight: 132,
              padding: '12px 14px',
              background: v6.inset,
              border: `1px solid ${v6.hair}`,
              ...v6.mono, fontSize: 11, color: d.notes ? v6.text : v6.faint,
              lineHeight: 1.6,
            }}>
              {d.notes || 'NO NOTES RECORDED.'}
            </div>
          </div>
        </div>

        {/* Actions */}
        <div style={{ display: 'flex', gap: 10, paddingTop: 6, borderTop: `1px solid ${v6.hair}` }}>
          {d.status !== 'acknowledged' && d.status !== 'resolved' && (
            <button style={{
              padding: '10px 18px',
              background: 'rgba(255,255,255,0.05)',
              border: `1px solid ${v6.hairBright}`,
              color: v6.text, cursor: 'pointer',
              ...v6.mono, fontSize: 10.5, letterSpacing: 1.8, fontWeight: 600,
            }}>ACKNOWLEDGE</button>
          )}
          {d.status !== 'resolved' && (
            <button style={{
              padding: '10px 18px',
              background: 'rgba(91,186,111,0.10)',
              border: `1px solid rgba(91,186,111,0.4)`,
              color: '#7ed98e', cursor: 'pointer',
              ...v6.mono, fontSize: 10.5, letterSpacing: 1.8, fontWeight: 600,
            }}>MARK RESOLVED</button>
          )}
          {d.status !== 'false_positive' && (
            <button style={{
              padding: '10px 18px',
              background: 'rgba(255,255,255,0.04)',
              border: `1px solid ${v6.hair}`,
              color: v6.mute, cursor: 'pointer',
              ...v6.mono, fontSize: 10.5, letterSpacing: 1.8, fontWeight: 600,
            }}>FALSE POSITIVE</button>
          )}
          <div style={{ flex: 1 }} />
          <button onClick={onSwitchManual} style={{
            padding: '10px 18px',
            background: OMV_NAVY_HI,
            border: `1px solid ${OMV_BLUE_GLOW}`,
            color: '#fff', cursor: 'pointer',
            ...v6.mono, fontSize: 10.5, letterSpacing: 1.8, fontWeight: 700,
            display: 'flex', alignItems: 'center', gap: 8,
          }}>
            <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M12 2 L12 22 M2 12 L22 12" /><circle cx="12" cy="12" r="2" fill="currentColor" />
            </svg>
            SWITCH TO MANUAL
          </button>
        </div>
      </div>
    );
  }

  function V6HistoryDetailPage({ initialSelected, onClose }) {
    const [filters, setFilters] = React.useState({
      priority: 'all',
      status: 'all',
      date: 'all',
      mission: 'all',
    });
    const [selectedId, setSelectedId] = React.useState(initialSelected?.id || DETECTIONS[0].id);

    // Filter detections
    const filtered = DETECTIONS.filter(d => {
      if (filters.priority !== 'all' && d.priority !== filters.priority) return false;
      if (filters.status !== 'all' && d.status !== filters.status) return false;
      if (filters.date === 'today' && d.date !== DETECTIONS[0].date) return false;
      if (filters.mission !== 'all' && d.mission !== filters.mission) return false;
      return true;
    }).sort((a, b) => {
      // Critical first, then high, etc.
      if (PRIORITY[a.priority].rank !== PRIORITY[b.priority].rank) {
        return PRIORITY[a.priority].rank - PRIORITY[b.priority].rank;
      }
      // Then by date desc
      return (b.date + ' ' + b.time).localeCompare(a.date + ' ' + a.time);
    });

    // Counts per priority
    const counts = {
      all: DETECTIONS.length,
      critical: DETECTIONS.filter(d => d.priority === 'critical').length,
      high: DETECTIONS.filter(d => d.priority === 'high').length,
      medium: DETECTIONS.filter(d => d.priority === 'medium').length,
      low: DETECTIONS.filter(d => d.priority === 'low').length,
    };

    const selected = filtered.find(d => d.id === selectedId) || filtered[0] || null;
    const missions = [...new Set(DETECTIONS.map(d => d.mission))];

    return (
      <div style={{
        position: 'absolute', inset: 0, zIndex: 200,
        background: v6.canvas,
        display: 'flex', flexDirection: 'column',
        animation: 'slideUp 0.28s cubic-bezier(.4,.2,.2,1) forwards',
      }}>
        {/* Inject animation keyframe once */}
        <style>{`@keyframes slideUp { from { transform: translateY(20px); opacity: 0; } to { transform: translateY(0); opacity: 1; } }`}</style>

        {/* Header */}
        <div style={{
          height: 60, flex: '0 0 60px',
          background: v6.surface,
          borderBottom: `1px solid ${v6.hair}`,
          display: 'flex', alignItems: 'center', padding: '0 22px', gap: 22,
        }}>
          <button onClick={onClose} style={{
            background: 'transparent', border: `1px solid ${v6.hairBright}`,
            color: v6.text, padding: '7px 14px', cursor: 'pointer',
            display: 'flex', alignItems: 'center', gap: 8,
            ...v6.mono, fontSize: 10.5, letterSpacing: 1.5,
          }}
          onMouseEnter={(e) => { e.currentTarget.style.borderColor = OMV_BLUE_GLOW; }}
          onMouseLeave={(e) => { e.currentTarget.style.borderColor = v6.hairBright; }}>
            <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M19 12H5 M12 19l-7-7 7-7" /></svg>
            BACK TO LIVE
          </button>
          <div style={{ width: 1, height: 30, background: v6.hair }} />
          <div>
            <div style={{ ...v6.mono, fontSize: 9.5, letterSpacing: 2.5, color: v6.mute, textTransform: 'uppercase' }}>Detections · Archive</div>
            <div style={{ fontFamily: '"Helvetica Neue", Helvetica, Arial, sans-serif', fontSize: 17, color: '#fff', lineHeight: 1, marginTop: 5, fontWeight: 500, letterSpacing: 0.5 }}>ALL LEAKS · EVERY PRIORITY</div>
          </div>
          <div style={{ flex: 1 }} />
          <span style={{ ...v6.mono, fontSize: 10.5, color: v6.mute, letterSpacing: 1.2 }}>
            <span style={{ color: '#fff' }}>{filtered.length}</span> / {DETECTIONS.length} SHOWN
          </span>
        </div>

        {/* Filter bar */}
        <div style={{
          flex: '0 0 auto',
          background: v6.surface,
          borderBottom: `1px solid ${v6.hair}`,
          padding: '14px 22px',
          display: 'flex', flexDirection: 'column', gap: 10,
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
            <span style={{ ...v6.mono, fontSize: 9, letterSpacing: 1.8, color: v6.faint, width: 72 }}>PRIORITY</span>
            {['all', 'critical', 'high', 'medium', 'low'].map(k => (
              <PriorityFilterPill key={k} k={k}
                label={k === 'all' ? 'ALL' : PRIORITY[k].label}
                active={filters.priority === k}
                count={counts[k]}
                onClick={() => setFilters({ ...filters, priority: k })} />
            ))}
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 14, flexWrap: 'wrap' }}>
            <span style={{ ...v6.mono, fontSize: 9, letterSpacing: 1.8, color: v6.faint, width: 72 }}>STATUS</span>
            <StatusFilterPill k="all" label="ALL" active={filters.status === 'all'} onClick={() => setFilters({ ...filters, status: 'all' })} />
            {Object.entries(STATUS).map(([k, s]) => (
              <StatusFilterPill key={k} k={k} label={s.label} active={filters.status === k} onClick={() => setFilters({ ...filters, status: k })} />
            ))}
            <div style={{ width: 1, height: 18, background: v6.hair, marginLeft: 6, marginRight: 6 }} />
            <span style={{ ...v6.mono, fontSize: 9, letterSpacing: 1.8, color: v6.faint }}>DATE</span>
            {[['all', 'ALL TIME'], ['today', 'TODAY']].map(([k, label]) => (
              <StatusFilterPill key={k} k={k} label={label} active={filters.date === k} onClick={() => setFilters({ ...filters, date: k })} />
            ))}
            <div style={{ width: 1, height: 18, background: v6.hair, marginLeft: 6, marginRight: 6 }} />
            <span style={{ ...v6.mono, fontSize: 9, letterSpacing: 1.8, color: v6.faint }}>MISSION</span>
            <select
              value={filters.mission}
              onChange={(e) => setFilters({ ...filters, mission: e.target.value })}
              style={{
                background: v6.inset,
                border: `1px solid ${v6.hair}`,
                color: v6.text,
                ...v6.mono, fontSize: 9.5, letterSpacing: 1.2,
                padding: '5px 10px',
                cursor: 'pointer',
              }}>
              <option value="all">ALL MISSIONS</option>
              {missions.map(m => <option key={m} value={m}>{m}</option>)}
            </select>
          </div>
        </div>

        {/* Main split */}
        <div style={{ flex: 1, display: 'flex', minHeight: 0 }}>
          {/* List */}
          <div style={{
            width: 360, flex: '0 0 360px',
            borderRight: `1px solid ${v6.hair}`,
            background: v6.surface,
            overflow: 'auto',
          }}>
            {filtered.length === 0 ? (
              <div style={{ padding: 40, textAlign: 'center', color: v6.faint, ...v6.mono, fontSize: 11, letterSpacing: 1.5 }}>
                NO DETECTIONS MATCH FILTERS
              </div>
            ) : (
              filtered.map(d => (
                <DetectionListItem key={d.id} d={d} selected={d.id === selected?.id} onClick={() => setSelectedId(d.id)} />
              ))
            )}
          </div>
          {/* Detail */}
          <DetectionDetail d={selected} onSwitchManual={onClose} />
        </div>
      </div>
    );
  }

  window.V6HistoryDetailPage = V6HistoryDetailPage;
})();
