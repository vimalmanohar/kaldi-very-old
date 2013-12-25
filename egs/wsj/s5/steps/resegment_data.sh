#!/bin/bash

# Copyright Johns Hopkins University (Author: Daniel Povey) 2013.  Apache 2.0.

# This script segments speech data based on some kind of decoding of
# whole recordings (e.g. whole conversation sides.  See 
# egs/swbd/s5b/local/run_resegment.sh for an example of usage.
# You'll probably want to use the script resegment_text.sh

# begin configuration section.
stage=0
cmd=run.pl
cleanup=true
segmentation_opts="--max-length-diff 0.4"  # E.g. set this as --segmentation-opts "--silence-proportion 0.2 --max-segment-length 10"
rttm_based_map=true

#end configuration section.

[ -f ./path.sh ] && . ./path.sh
. parse_options.sh || exit 1;

if [ $# -ne 5 ]; then
  echo "Usage: $0 [options] <in-data-dir> <lang> <decode-dir|ali-dir> <out-data-dir> <temp/log-dir>"
  echo " Options:"
  echo "    --cmd (run.pl|queue.pl...)      # specify how to run the sub-processes."
  echo "    --stage (0|1|2)                 # start scoring script from part-way through."
  echo "    --segmentation-opts '--opt1 opt1val --opt2 opt2val' # options for segmentation.pl"
  echo "e.g.:"
  echo "$0 data/train_unseg exp/tri3b/decode_train_unseg data/train_seg exp/tri3b_resegment"
  exit 1;
fi

data=$1
lang=$2
alidir=$3 # may actually be decode-dir.
data_out=$4
dir=$5

mkdir -p $data_out || exit 1;
rm $data_out/* 2>/dev/null # Old stuff that's partial can cause problems later if
                           # we call fix_data_dir.sh; it will cause things to be 
                           # thrown out.
mkdir -p $dir/log || exit 1;

for f in $data/feats.scp $lang/phones.txt $alidir/ali.1.gz $alidir/num_jobs; do
  if [ ! -f $f ]; then 
    echo "$0: no such file $f"
    exit 1;
    fi
done

if [ -f $alidir/final.mdl ]; then
  model=$alidir/final.mdl
else
  if [ ! -f $alidir/../final.mdl ]; then
    echo "$0: found no model in $alidir/final.mdl or $alidir/../final.mdl"
    exit 1;
  fi
  model=$alidir/../final.mdl
fi

# get lists of sil,noise,nonsil phones
# convert *.ali.gz to *.ali.gz with 0,1,2.
# run perl script..
# output segments?


if ! [ `cat $lang/phones/optional_silence.txt | wc -w` -eq 1 ]; then
  echo "Error: this script only works if $lang/phones/optional_silence.txt contains exactly one entry.";
  echo "You'd have to modify the script to handle other cases."
  exit 1;
fi

silphone=`cat $lang/phones/optional_silence.txt` 
# silphone will typically be "sil" or "SIL". 

# 3 sets of phones: 0 is silence, 1 is noise, 2 is speech.,
if $rttm_based_map; then
  (
  echo "$silphone 0"
  grep -v -w $silphone $lang/phones/silence.txt \
    | awk '{print $1, 1;}' \
    | sed 's/SIL\(.*\)1/SIL\10/' \
    | sed 's/<oov>\(.*\)1/<oov>\12/'
  cat $lang/phones/nonsilence.txt | awk '{print $1, 2;}' | sed 's/\(<.*>.*\)2/\11/' | sed 's/<oov>\(.*\)1/<oov>\12/'
  ) > $dir/phone_map.txt
else
  (
  echo "$silphone 0"
  grep -v -w $silphone $lang/phones/silence.txt \
    | awk '{print $1, 1;}'
  cat $lang/phones/nonsilence.txt | awk '{print $1, 2;}'
  ) > $dir/phone_map.txt
fi

nj=`cat $alidir/num_jobs` || exit 1;
echo $nj > $dir/num_jobs

if [ $stage -le 0 ]; then
  if [ ! -f $dir/classes.1.gz ]; then
    $cmd JOB=1:$nj $dir/log/classify.JOB.log \
      ali-to-phones --per-frame=true "$model" "ark:gunzip -c $alidir/ali.JOB.gz|" ark,t:- \| \
      utils/int2sym.pl -f 2- $lang/phones.txt \| \
      utils/apply_map.pl -f 2- $dir/phone_map.txt \| \
      gzip -c '>' $dir/classes.JOB.gz || exit 1
  fi

  mkdir -p $dir/classes
  rm $dir/classes/*
  
  if [ -z $(ls $dir/classes) ] || [ ! -f $dir/classes.done ]; then
    for n in `seq $nj`; do gunzip -c $dir/classes.$n.gz; done \
      | awk '{print "echo \""$0"\" > '$dir'/classes/"$1".pred"}' \
      | bash -e
    touch $dir/classes.done
  fi

  #local/segmentation_joint_with_analysis.py --verbose 1 $segmentation_opts $dir/classes \
  #  2> $dir/log/joint_resegment.log | sort > $data_out/segments || exit 1
  local/segmentation.py --verbose 10 $segmentation_opts $dir/classes \
    2> $dir/log/resegment.log | sort > $data_out/segments || exit 1
fi

if [ $stage -le 1 ]; then
  if [ -f $data/reco2file_and_channel ]; then
    cp $data/reco2file_and_channel $data_out/reco2file_and_channel
  fi
  if [ -f $data/wav.scp ]; then
    cp $data/wav.scp $data_out/wav.scp
  else
    echo "Expected file $data/wav.scp to exist" # or there is really nothing to copy.
    exit 1
  fi
  for f in glm stm; do 
    if [ -f $data/$f ]; then
      cp $data/$f $data_out/$f
    fi
  done

  [ ! -s $data_out/segments ] && echo "No data produced" && exit 1;

  # We'll make the speaker-ids be the same as the recording-ids (e.g. conversation
  # sides).  This will normally be OK for telephone data.
  cat $data_out/segments | awk '{print $1, $2}' > $data_out/utt2spk || exit 1
  utils/utt2spk_to_spk2utt.pl $data_out/utt2spk > $data_out/spk2utt || exit 1

  if $cleanup; then
    rm $dir/segments.*.gz
  fi
fi

cat $data_out/segments | awk '{num_secs += $4 - $3;} END{print "Number of hours of data is " (num_secs/3600);}'

