FROM mariadb@sha256:ef34af04bda12e6c85395328af78d562176c34fb29ae52063a4eb0d68fa7b3e9

RUN apt-get update && \
    apt-get install -y --no-install-recommends curl gcc libc6-dev zlib1g-dev && \
    rm -rf /var/lib/apt/lists/*

COPY watchdog/watchdog.c /tmp/watchdog.c
RUN gcc -O2 -Wall -o /usr/local/bin/watchdog /tmp/watchdog.c -lz -lpthread && \
    rm /tmp/watchdog.c

RUN apt-get purge -y --auto-remove gcc libc6-dev zlib1g-dev

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
CMD ["mariadbd"]
