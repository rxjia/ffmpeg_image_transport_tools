#!/usr/bin/env python

import os
import re
import subprocess

from tqdm import tqdm

def detect_walk_file(dir_path, rematch=None):
    """ example: rematch=r'.*\.json$' """
    l_files = []
    for root, dirs, files in os.walk(dir_path):
        if rematch is None:
            json_fnames = [os.path.join(root, fname) for fname in files]
        else:
            json_fnames = [os.path.join(root, fname) for fname in files if re.match(rematch, fname)]
        l_files.extend(json_fnames)
    l_files.sort()
    return l_files


if __name__ == '__main__':
    from argparse import ArgumentParser
    parser = ArgumentParser()
    parser.add_argument("--bag_dir", type=str, default=None)

    args = parser.parse_args()
    files = detect_walk_file(args.bag_dir, r'.*\.bag$')

    print(len(files))
    # files = filter(lambda x: (x.find('/debug/') == -1) and (x.find('train') == -1), files)
    # files = list(files)
    # print(len(files))

    for f in tqdm(files):
        bagfile = os.path.join(f)
        p=subprocess.Popen(f"roslaunch ffmpeg_image_transport_tools extract_ffmpeg.launch bag:={bagfile} write_time_stamps:=false video_rate:=30 output:=''",shell=True)
        p.wait()