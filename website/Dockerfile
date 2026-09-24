FROM python:3.13-slim
WORKDIR /app
COPY requirements.txt /app/

RUN apt-get update && \
    apt-get install -y --no-install-recommends netcat-openbsd libmariadb-dev gcc curl && \
    rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir -r requirements.txt
COPY . /app
EXPOSE 5000

RUN mkdir /app/key



RUN useradd -m lain && \
    chown -R lain:lain /app && \
    chmod 500 /app && \
    chmod 500 /app/* && \
    chmod 700 /app/key

USER lain

CMD ["gunicorn", "--bind", "0.0.0.0:5000", "--workers=2", "lain_app:app"]
