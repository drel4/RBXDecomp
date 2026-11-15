#!/bin/sh
set -eu
mkdir -p /data/places /data/thumbs /data/jobs
exec xvfb-run -a /venv/bin/gunicorn --bind 127.0.0.1:4380 --workers 1 --threads 8 --timeout 180 --access-logfile - app:app
