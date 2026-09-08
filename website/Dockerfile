FROM python:latest
WORKDIR /app
COPY requirements.txt /app/
RUN pip install --no-cache-dir -r requirements.txt
COPY . /app
EXPOSE 5000


RUN gcc /app/getflag.c -o /app/getflag && \
    rm /app/getflag.c

RUN mkdir /app/key

RUN useradd -m lain && \
    chown -R lain:lain /app && \
    chmod 500 /app && \
    chmod 500 /app/* && \
    chmod 700 /app/key

RUN chmod 400 /app/flag.txt
RUN chown root:root /app/flag.txt
RUN chown root:root /app/getflag
RUN chmod 4755 /app/getflag
USER lain

CMD ["python", "lain_app.py"]
