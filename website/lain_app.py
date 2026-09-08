from flask import Flask, render_template, request, redirect, url_for, session, render_template_string, jsonify
import uuid
import os
import bcrypt
from lain_secret import secret_key
import glob

app = Flask(__name__)
app.secret_key = secret_key
files_dir = 'lainzines/'

users = [
    ('lain', bcrypt.hashpw(os.urandom(64), bcrypt.gensalt(14)).decode('utf-8'))
]

def validate_user(username, password):
    for user in users:
        if username == user[0]:
            if bcrypt.checkpw(user[1], password):
                return True
    return False

@app.route('/')
def main():
    return render_template('home.html')

def default(title, content):
    return render_template('default.html', title=title, content=content)

@app.route('/search', methods=['GET'])
def search():
    query = request.args.get('q', '').lower()
    if not query:
        return default('error', 'no such file!')
    
    file_path = os.path.join(files_dir, query)
    try:
        file_content = open(file_path, 'r').read()
        return default('file-acq', file_content)
    except:
        return default('error', 'something went wrong... try reading "lain1.txt" for an example file.')

# file upload is currently disabled
@app.route('/upload', methods=['GET', 'POST'])
def upload():
    if request.method == 'POST':
        filename = request.form["filename"]
        content = request.form["content"]
        
        if not filename or not content:
            return default('error', 'filename and content are required...')
        
        file_path = os.path.join(files_dir, filename)

        if len(glob.glob(file_path)) > 0:
            return default('error', 'file already exists...')

        return default('error', 'file uploading is currently disabled.')

    return render_template('upload.html')
    
@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        username = request.form["username"]
        password = request.form["password"].encode('utf-8')
        user = validate_user(username, password)
        if user:
            session["user_id"] = user["id"]
            session["uuid"] = user["uuid"]
            session["is_admin"] = (username == "admin")
            return redirect(url_for("lain"))
        return default('error', 'invalid credentials...')
    return render_template("login.html")

@app.route("/lain")
def lain():
    if not session.get("is_admin"):
        return redirect(url_for("login"))
    
    # for Lain's debugging purposes
    file = open('templates/dashboard.html').read().replace('[LAIN]', session.get('uuid'))
    return render_template_string(file)

@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for("login"))

if __name__ == "__main__":
    app.run(host='0.0.0.0')
