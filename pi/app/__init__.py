"""
Flask application factory.
"""

from flask import Flask
from flask_socketio import SocketIO

from .routes.control import control_bp
from .routes.inspection import inspection_bp
from .routes.camera import camera_bp
from .routes.results import results_bp

socketio = SocketIO()


def create_app():
    app = Flask(__name__, static_folder="static", static_url_path="")
    app.register_blueprint(control_bp,    url_prefix="/api")
    app.register_blueprint(inspection_bp, url_prefix="/api/inspection")
    app.register_blueprint(camera_bp,     url_prefix="/api/camera")
    app.register_blueprint(results_bp,    url_prefix="/api/results")
    socketio.init_app(app)
    return app, socketio
