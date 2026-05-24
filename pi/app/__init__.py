"""
Flask application factory for the Argus backend.

Serves the Argus single-page UI, the camera MJPEG streams, the REST control
surface, and the SocketIO live channel. The FLIR camera service and ESP32
client are created once at startup and shared through app.services.runtime.
"""

import logging
import os
import re

from flask import Flask, Response
from flask_socketio import SocketIO

# Defined before the blueprints are imported: several blueprint modules do
# ``from app import socketio`` at import time.
socketio = SocketIO()

from .routes.control import control_bp          # noqa: E402
from .routes.inspection import inspection_bp    # noqa: E402
from .routes.camera import camera_bp            # noqa: E402
from .routes.results import results_bp          # noqa: E402

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger(__name__)


def create_app():
    app = Flask(__name__, static_folder="static", static_url_path="")

    socketio.init_app(app, cors_allowed_origins="*", async_mode="threading")

    app.register_blueprint(control_bp,    url_prefix="/api")
    app.register_blueprint(camera_bp,     url_prefix="/api/camera")
    app.register_blueprint(inspection_bp, url_prefix="/api/inspection")
    app.register_blueprint(results_bp,    url_prefix="/api/results")

    @app.route("/")
    def index():
        return _serve_index(app.static_folder)

    _init_services(app)
    return app, socketio


# Matches local script sources like src="v6-argus.jsx" but NOT the https://unpkg
# CDN scripts (those contain ':' and '/', which the character class excludes).
_LOCAL_JSX_SRC = re.compile(r'src="([^":/?#]+\.jsx)"')


def _serve_index(static_folder):
    """Serve Argus.html with a cache-busting ``?v=<mtime>`` appended to each
    local ``.jsx`` ``<script src>``.

    The browser caches the .jsx bundle, so after editing a component (e.g. the
    1920×1080 canvas size) a plain reload can replay the stale copy. Stamping
    each src with the file's mtime changes the URL whenever the file changes,
    forcing a refetch; the HTML itself is sent ``no-store`` so this rewritten
    page is never cached either.
    """
    with open(os.path.join(static_folder, "Argus.html"), encoding="utf-8") as fh:
        html = fh.read()

    def _stamp(match):
        src = match.group(1)
        try:
            version = int(os.path.getmtime(os.path.join(static_folder, src)))
        except OSError:
            return match.group(0)            # file missing — leave src untouched
        return f'src="{src}?v={version}"'

    resp = Response(_LOCAL_JSX_SRC.sub(_stamp, html), mimetype="text/html")
    resp.headers["Cache-Control"] = "no-store"
    return resp


def _init_services(app):
    """Create the camera + ESP32 singletons and start the push tasks once."""
    from app.services import runtime
    from app.services.camera_base import get_camera
    from app.services.esp_client import ESP32Client
    from app import events

    if runtime.camera is None:
        try:
            runtime.camera = get_camera()
            log.info("camera service initialized")
        except Exception:                       # never let camera bring-up crash the server
            log.exception("camera init failed — feeds will be unavailable")
            runtime.camera = None

    if runtime.esp is None:
        runtime.esp = ESP32Client()

    app.camera = runtime.camera
    app.esp = runtime.esp
    events.init()
