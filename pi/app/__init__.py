"""
Flask application factory for the Argus backend.

Serves the Argus single-page UI, the camera MJPEG streams, the REST control
surface, and the SocketIO live channel. The FLIR camera service and ESP32
client are created once at startup and shared through app.services.runtime.
"""

import logging

from flask import Flask, send_from_directory
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
        return send_from_directory(app.static_folder, "Argus.html")

    _init_services(app)
    return app, socketio


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
