import hashlib
import os
import uuid

global secret_key

key_dir = 'key/'
os.makedirs(key_dir, exist_ok=True)

secret_file = [f for f in os.listdir('key/') if f.startswith('secret_')]
if len(secret_file) == 0:
    secret_key = hashlib.sha256(os.urandom(32)).hexdigest()
    filename = 'key/secret_' + str(uuid.uuid4())
    with open(filename, 'w') as f:
        f.write(secret_key)
        f.close()
else:
    secret_key = open('key/' + secret_file[0]).read()
