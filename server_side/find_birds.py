# find_birds.py
# checks a directory occassionally, and if it finds something, it processes it

from birdnetlib import Recording
from birdnetlib.analyzer import Analyzer
from datetime import datetime
import time
import os

pending_dir = "pending"
finished_dir = "finished"

def current_files(): 
    # store all files found in the pending directory
    filenames = []
    for filename in os.listdir(pending_dir):
        f = os.path.join(pending_dir, filename)
        # checking if it is a file
        if os.path.isfile(f):
            filenames.append(filename)
    
    return filenames

# Load and initialize the BirdNET-Analyzer models.
analyzer = Analyzer()

while True:
    files = current_files()
    for filename in files:
        recording = Recording(
            analyzer,
            pending_dir + "/" + filename,
            lat=35.4244,
            lon=-120.7463,
            date=datetime.now(), # use date or week_48
            min_conf=0.25,
        )
        recording.analyze()
        with open(finished_dir + "/" + filename, "wb") as f:
            with open(pending_dir + "/" + filename, "rb") as g:
                f.write(g.read())
        with open(finished_dir + "/result_" + filename, "w") as f:
            f.write(str(recording.detections))
    time.sleep(20)