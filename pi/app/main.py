"""
Entry point: python -m app.main
"""

from app import create_app
from app.config import FLASK_HOST, FLASK_PORT, FLASK_DEBUG

app, socketio = create_app()

if __name__ == "__main__":
    socketio.run(app, host=FLASK_HOST, port=FLASK_PORT, debug=FLASK_DEBUG, allow_unsafe_werkzeug=True)
