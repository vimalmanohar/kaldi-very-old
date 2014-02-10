#! /usr/bin/python

import os, argparse, sys, textwrap
from argparse import ArgumentParser

def main():
  parser = ArgumentParser(description=textwrap.dedent('''\
      Convert a list of VAD files to segments.

      The program takes a list of vad fies possibly from stdin and
      outputs a segments file in the usual Kaldi format.

      The VAD files are the format:
      <start-time> <end-time>
      where start time and end time are times * 100 stored as integers.

      The segments file is in the usual Kaldi format.

      This is typically used with VAD generated by non-speech remover
      such as the one based on Poisson Point Process.'''), \
          formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument('--verbose', type=int, \
      dest='verbose', default=0, \
      help='Give higher verbose for more logging (default: %(default)s)')

  parser.add_argument('--frame-shift', type=float, \
      dest='frame_shift', default=0.01, \
      help="Time difference between adjacent frame (default: %(default)s)s")
