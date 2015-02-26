#ifndef KALLISTO_PROCESSREADS_H
#define KALLISTO_PROCESSREADS_H

#include <zlib.h>
#include "kseq.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include <iostream>
#include <fstream>

#include <seqan/bam_io.h>


#include "common.h"

using namespace seqan;

template<typename Index, typename TranscriptCollector>
void ProcessReads(Index& index, const ProgramOptions& opt, TranscriptCollector &tc) {

  // need to receive an index map
  std::ios_base::sync_with_stdio(false);


  bool paired = (opt.files.size() == 2);

  gzFile fp1 = 0, fp2 = 0;
  kseq_t *seq1 = 0, *seq2;
  std::vector<std::pair<int,int>> v;
  v.reserve(1000);

  int l1,l2; // length of read
  size_t nreads = 0;


  // for each file

  fp1 = gzopen(opt.files[0].c_str(), "r");
  seq1 = kseq_init(fp1);
  if (paired) {
    fp2 = gzopen(opt.files[1].c_str(),"r");
    seq2 = kseq_init(fp2);
  }


  // for each read
  while (true) {
    l1 = kseq_read(seq1);
    if (paired) {
      l2 = kseq_read(seq2);
    }
    if (l1 <= 0) {
      break;
    }
    if (paired && l2 <= 0) {
      break;
    }

    nreads++;
    v.clear();
    // process read
    index.match(seq1->seq.s, seq1->seq.l, v);
    if (paired) {
      int vl = v.size();
      index.match(seq2->seq.s, seq2->seq.l, v);
      // fix k-mer positions, assuming an average
      // fragment length distribution of fld.
      int leftpos = ((int) opt.fld)-opt.k;
      for (int i = vl; i < v.size(); i++) {
        v[i].second = std::max(leftpos-v[i].second, 0);
      }
    }

    // collect the transcript information
    int ec = tc.collect(v);
    if (opt.verbose && nreads % 10000 == 0 ) {
      std::cerr << "Processed " << nreads << std::endl;
    }
  }
  gzclose(fp1);
  if (paired) {
    gzclose(fp2);
  }

  kseq_destroy(seq1);
  if (paired) {
    kseq_destroy(seq2);
  }

  // write output to outdir
  std::string outfile = opt.output + "/counts.txt"; // figure out filenaming scheme
  std::ofstream of;
  of.open(outfile.c_str(), std::ios::out);
  tc.write(of);
  of.close();
}

template<typename Index, typename TranscriptCollector>
void ProcessBams(Index& index, const ProgramOptions& opt, TranscriptCollector& tc) {

  // need to receive an index map
  std::ios_base::sync_with_stdio(false);
  std::vector<std::pair<int,int>> v;
  v.reserve(1000);


  std::vector<int> rIdToTrans;

  int l1,l2; // length of read
  size_t nreads = 0;

  BamFileIn bamFileIn(opt.files[0].c_str());
  /*
  if (!open(bamFileIn, opt.files[0].c_str())) {
    std::cerr << "ERROR: Could not open bamfile " << opt.files[0] << std::endl;
    exit(1);
    }*/

  BamHeader header;
  readHeader(header, bamFileIn);

  //typedef FormattedFileContext<BamFileIn, void>::Type TBamContext;

  ///BamIOContext<StringSet<CharString>>
  auto const & bamContext = context(bamFileIn);
  
  {
    std::unordered_map<std::string, int> inv;
    for (int i = 0; i < index.target_names_.size(); i++) {
      inv.insert({index.target_names_[i], i});
    }
    auto cnames = contigNames(bamContext);
    for (int i = 0; i < length(cnames[i]); i++) {
      std::string cname(toCString(cnames[i]));
      auto search = inv.find(cname);
      if (search == inv.end()) {
        std::cerr << "Error: could not find transcript name " << cname << " from bam file " << opt.files[0] << std::endl;
      } else {
        rIdToTrans.push_back(search->second);
      }
    }
  }

  
  

  // for each read
  BamAlignmentRecord record;
  if (atEnd(bamFileIn)) {
    std::cerr << "Warning: Empty bam file " << opt.files[0] << std::endl;
    return;
  }
  readRecord(record, bamFileIn);
  std::vector<int> p;
  CharString s1,s2;
  CharString lastName;
  bool done = false;

  int mismatches = 0;
  int alignKnotB = 0;
  int alignBnotK = 0;
  int exactMatches = 0;
  int alignNone = 0;
  
  while (!done) {
    bool paired = hasFlagMultiple(record);
    p.clear();
    clear(s1);
    clear(s2);
    
    // we haven't processed record yet
    while (true) {
      // collect sequence
      if (hasFlagFirst(record) && empty(s1)) {
        s1 = record.seq;
        if (hasFlagRC(record)) {
          reverseComplement(s1);
        }
      } else if (paired && hasFlagLast(record) && empty(s2)) {
        s2 = record.seq;
        if (hasFlagRC(record)) {
          reverseComplement(s2);
        }
      } else {
        if (empty(s1) && empty(s2)) {
          std::cerr << "Warning: weird sequence in bam file " << opt.files[0] << std::endl;
        }
      }


      // gather alignment info
      if (!hasFlagUnmapped(record)) {
        p.push_back(rIdToTrans[record.rID]);
      }

      // are there more reads?
      if (atEnd(bamFileIn)) {
        done = true;
        break;
      }
      
      // look at next read
      lastName = record.qName;
      readRecord(record, bamFileIn);
      if (lastName != record.qName) {
        break; // process the record next time
      }
    }
    
    if (empty(s1) || empty(s2)) {
      std::cerr << "Warning: only one read is present " << std::endl << s1 << std::endl << s2 << std::endl;
    }
    nreads++;
    

    
    v.clear();
    // process read
    index.match(toCString(s1), length(s1), v);
    if (paired) {
      int vl = v.size();
      index.match(toCString(s2), length(s2), v);
      // fix k-mer positions, assuming an average
      // fragment length distribution of fld.
      int leftpos = ((int) opt.fld)-opt.k;
      for (int i = vl; i < v.size(); i++) {
        v[i].second = std::max(leftpos-v[i].second, 0);
      }
    }

    // collect the transcript information
    int ec = tc.collect(v);

    if (opt.verbose && nreads % 10000 == 0 ) {
      std::cerr << "Processed " << nreads << std::endl;
    }

    sort(p.begin(), p.end());
    std::vector<int> lp;
    if (!p.empty()) {
      lp.push_back(p[0]);
      for (int i = 1; i < p.size(); i++) {
        if (p[i-1] != p[i]) {
          lp.push_back(p[i]);
        }
      }
    }

    if (!lp.empty()) {
      auto find = index.ecmapinv.find(lp);
      if (find != index.ecmapinv.end()) {
        if (find->second != ec) {
          // mismatch
          if (ec == -1) {
            alignBnotK++;
          } else {
            mismatches++;
          }
        } else {
          exactMatches++;
        }
      }
    } else {
      if (ec >= 0) {
        alignKnotB++;
      } else {
        alignNone++;
      }
    }
  }

  std::cout << "Aligned " <<  nreads << std::endl
            << "exact matches = " << exactMatches << std::endl
            << "Kallisto mapped, not BAM = " << alignKnotB << std::endl
            << "Bam mapped, not Kallisto = " << alignBnotK << std::endl
            << "Both mapped, mismatches = " << mismatches << std::endl
            << "Neither mapped = " << alignNone << std::endl;
  
  // writeoutput to outdir
  std::string outfile = opt.output + "/counts.txt"; // figure out filenaming scheme
  std::ofstream of;
  of.open(outfile.c_str(), std::ios::out);
  tc.write(of);
  of.close();

  return;
}


#endif // KALLISTO_PROCESSREADS_H
