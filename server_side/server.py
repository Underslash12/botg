# server.py

from flask import Flask
from flask import request
import base64c
import os

app = Flask(__name__)

def concat_files(total):
    with open("full_file.pcm", "wb") as f:
        for i in range(total):
            with open(f"temp/{i}.raw", "rb") as g:
                f.write(g.read())
    os.system("ffmpeg -f s16le -ar 22.05k -ac 1 -i full_file.pcm pending/file.wav")      

@app.route("/")
def hello():
    index = int(request.args.get("i"))
    total = int(request.args.get("t"))
    data = request.args.get("d")

    print(index, total, len(data))

    with open(f"temp/{index}.raw", "wb") as f:
        f.write(base64c.decode_from_str_to_bytes(data))

    if index == total - 1:
        concat_files(total)

    return "Hi"
    # return "Received value" + str(request.args.get("var"))