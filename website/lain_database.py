#!/usr/bin/env python3

import mariadb

def initialize_database(filename, secret):
    conn = mariadb.connect(
        host="mariadb",
        user="example-user",
        password="my_cool_secret",
        database="appdb",
        port=3306,
    )

    cur = conn.cursor()

    cur.execute("""
        CREATE TABLE IF NOT EXISTS SECRETS (
            filename VARCHAR(64),
            secret VARCHAR(64)
        )
    """)

    cur.execute(
        "INSERT INTO SECRETS (filename, secret) VALUES (?, ?)",
        (filename, secret)
    )

    conn.commit()

    cur.close()
    conn.close()
