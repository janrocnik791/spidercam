/**
 * SpiderCam – Live Camera Feed
 *
 * The thermal image is streamed via MJPEG at /api/camera/stream and set
 * directly as the <img> src in index.html — no JS needed for the image itself.
 *
 * This script polls /api/camera/frame.json every second to keep the
 * #temp-min / #temp-max display current.
 */

(function pollTemps() {
  fetch('/api/camera/frame.json')
    .then(r => r.json())
    .then(d => {
      const minEl = document.getElementById('temp-min');
      const maxEl = document.getElementById('temp-max');
      if (minEl) minEl.textContent = d.min.toFixed(1) + '°C';
      if (maxEl) maxEl.textContent = d.max.toFixed(1) + '°C';
    })
    .catch(() => {})
    .finally(() => setTimeout(pollTemps, 1000));
})();
