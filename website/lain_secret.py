import hashlib
import os
import uuid
from lain_database import initialize_database

global secret_key

key_dir = 'key/'
os.makedirs(key_dir, exist_ok=True)

secret_file = [f for f in os.listdir('key/') if f.startswith('secret_')]
if len(secret_file) == 0:
    secret_key = hashlib.sha256(os.urandom(32)).hexdigest()
    fn = str(uuid.uuid4())
    filename = 'key/secret_' + fn
    with open(filename, 'w') as f:
        f.write(secret_key)
        f.close()
    initialize_database(fn, secret_key)
else:
    secret_key = open('key/' + secret_file[0]).read()
