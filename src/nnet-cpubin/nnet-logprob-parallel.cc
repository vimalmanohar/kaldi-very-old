// nnet-cpubin/nnet-logprob-parallel.cc

// Copyright 2012  Johns Hopkins University (author: Daniel Povey)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "hmm/transition-model.h"
#include "nnet-cpu/nnet-randomize.h"
#include "nnet-cpu/nnet-update-parallel.h"
#include "nnet-cpu/am-nnet.h"
#include "thread/kaldi-task-sequence.h"

namespace kaldi {
namespace nnet2 {

struct NnetLogprobTask {
  NnetLogprobTask(const AmNnet &am_nnet,
                  const Vector<BaseFloat> &inv_priors,
                  const std::string &key,
                  const Matrix<BaseFloat> &feats,
                  const Vector<BaseFloat> &spk_vec,
                  BaseFloatMatrixWriter *logprob_writer):
      am_nnet_(am_nnet), inv_priors_(inv_priors), key_(key), feats_(feats),
      spk_vec_(spk_vec), logprob_writer_(logprob_writer) { }
  void operator () () {
    log_probs_.Resize(feats_.NumRows(), am_nnet_.NumPdfs());
    bool pad_input = true;
    NnetComputation(am_nnet_.GetNnet(), feats_, spk_vec_, pad_input,
                    &log_probs_);
  }

  ~NnetLogprobTask() { // Produces output.  Run sequentially.
    // at this point they are probabilities, not log-probs, without prior division.
    
    log_probs_.MulColsVec(inv_priors_); // scales each column by the corresponding element
    // of inv_priors.
    for (int32 i = 0; i < log_probs_.NumRows(); i++) {
      SubVector<BaseFloat> frame(log_probs_, i);
      BaseFloat p = frame.Sum();
      if (!(p > 0.0)) {
        KALDI_WARN << "Bad sum of probabilities " << p;
      } else {
        frame.Scale(1.0 / p); // re-normalize to sum to one.
      }
    }
    log_probs_.ApplyFloor(1.0e-20); // To avoid log of zero which leads to NaN.
    log_probs_.ApplyLog();
    logprob_writer_->Write(key_, log_probs_);
  }

 private:
  const AmNnet &am_nnet_;
  const Vector<BaseFloat> &inv_priors_;
  std::string key_;
  Matrix<BaseFloat> feats_;
  Vector<BaseFloat> spk_vec_;
  Matrix<BaseFloat> log_probs_;
  BaseFloatMatrixWriter *logprob_writer_;
};

} // namespace nnet2
} // namespace kaldi


int main(int argc, char *argv[]) {
  try {
    using namespace kaldi;
    using namespace kaldi::nnet2;
    typedef kaldi::int32 int32;
    typedef kaldi::int64 int64;

    const char *usage =
        "Do the forward computation for a neural net acoustic model, and output\n"
        "matrix of logprobs (including division by prior).\n"
        "\n"
        "Usage: nnet-logprob-parallel [options] <model-in> <features-rspecifier> "
        "<logprobs-wspecifier>\n"
        "\n"
        "e.g.: nnet-logprob-parallel 1.nnet \"$feats\" ark:- | latgen-faster-mapped [args]\n";
    
    std::string spk_vecs_rspecifier, utt2spk_rspecifier;
    TaskSequencerConfig thread_config;
    
    ParseOptions po(usage);
    
    po.Register("spk-vecs", &spk_vecs_rspecifier, "Rspecifier for a vector that "
                "describes each speaker; only needed if the neural net was "
                "trained this way.");
    po.Register("utt2spk", &utt2spk_rspecifier, "Rspecifier for map from "
                "utterance to speaker; only relevant in conjunction with the "
                "--spk-vecs option.");
    thread_config.Register(&po);
    
    po.Read(argc, argv);
    
    if (po.NumArgs() != 3) {
      po.PrintUsage();
      exit(1);
    }
    
    std::string nnet_rxfilename = po.GetArg(1),
        feats_rspecifier = po.GetArg(2),
        logprob_wspecifier = po.GetArg(3);
    
    TransitionModel trans_model;
    AmNnet am_nnet;
    {
      bool binary_read;
      Input ki(nnet_rxfilename, &binary_read);
      trans_model.Read(ki.Stream(), binary_read);
      am_nnet.Read(ki.Stream(), binary_read);
    }

    int64 num_done = 0, num_err = 0;

    Vector<BaseFloat> inv_priors(am_nnet.Priors());
    KALDI_ASSERT(inv_priors.Dim() == am_nnet.NumPdfs() &&
                 "Priors in neural network not set up.");
    inv_priors.ApplyPow(-1.0);
    
    SequentialBaseFloatMatrixReader feature_reader(feats_rspecifier);
    // note: spk_vecs_rspecifier and utt2spk_rspecifier may be empty.
    RandomAccessBaseFloatVectorReaderMapped vecs_reader(spk_vecs_rspecifier,
                                                        utt2spk_rspecifier);
    BaseFloatMatrixWriter logprob_writer(logprob_wspecifier);

    {
      TaskSequencer<NnetLogprobTask> sequencer(thread_config);
    
      for (; !feature_reader.Done(); feature_reader.Next()) {
        std::string key = feature_reader.Key();
        const Matrix<BaseFloat> &feats = feature_reader.Value();
        Vector<BaseFloat> spk_vec;
        if (!spk_vecs_rspecifier.empty()) {
          if (!vecs_reader.HasKey(key)) {
            KALDI_ERR << "No speaker vector available for key " << key;
            num_err++;
            continue;
          }
          spk_vec = vecs_reader.Value(key);
        }

        sequencer.Run(new NnetLogprobTask(am_nnet, inv_priors, key, feats,
                                          spk_vec, &logprob_writer));
        num_done++;
      }
    }
    
    KALDI_LOG << "Finished computing neural net log-probs, processed "
              << num_done << " utterances, " << num_err << " with errors.";
    return (num_done == 0 ? 1 : 0);
  } catch(const std::exception &e) {
    std::cerr << e.what() << '\n';
    return -1;
  }
}


