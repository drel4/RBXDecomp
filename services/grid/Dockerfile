FROM debian:trixie-slim
RUN dpkg --add-architecture i386 && apt-get update && \
    apt-get install -y --no-install-recommends python3 python3-venv wine wine32:i386 wine64 xvfb xauth ca-certificates \
      libcurl4t64 libssl3t64 libstdc++6 \
      libcurl4t64:i386 libssl3t64:i386 libstdc++6:i386 && \
    rm -rf /var/lib/apt/lists/*
COPY requirements.txt /app/requirements.txt
RUN python3 -m venv /venv && /venv/bin/pip install --no-cache-dir -r /app/requirements.txt
RUN useradd -u 1000 -m grid
WORKDIR /app
COPY . /app
RUN sed -i 's/\r$//' /app/entrypoint.sh && chmod 755 /app/entrypoint.sh
USER grid
ENV WINEPREFIX=/data/wine WINEARCH=win32 WINEDEBUG=-all GRID_DATA=/data
ENV WINEDLLOVERRIDES="mscoree,mshtml=d;opengl32,glu32=n"
ENTRYPOINT ["/app/entrypoint.sh"]
