"""
Entry point: python -m app.main
"""

import os

from app import create_app, config
from app.config import FLASK_HOST, FLASK_PORT, FLASK_DEBUG


def _ensure_data_dirs():
    """Create the inspection / baseline / demo data folders if missing."""
    for d in config.DATA_DIRS:
        os.makedirs(d, exist_ok=True)


_ensure_data_dirs()
app, socketio = create_app()

if __name__ == "__main__":
    socketio.run(app, host=FLASK_HOST, port=FLASK_PORT, debug=FLASK_DEBUG, allow_unsafe_werkzeug=True)
