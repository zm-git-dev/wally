#ifndef REGION_H
#define REGION_H


#include <iostream>
#include <fstream>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/timer/timer.hpp>
#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/filesystem.hpp>

#include <htslib/faidx.h>
#include <htslib/vcf.h>
#include <htslib/sam.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <iostream>

#include "version.h"
#include "util.h"
#include "draw.h"

namespace wallysworld
{

  // Config arguments
  struct Config {
    uint32_t minMapQual;
    uint32_t maxCov;
    uint32_t width;
    uint32_t height;
    uint32_t tlheight;  // pixel height of a track line
    uint32_t rdheight;  // pixel height of a single read
    std::string regionStr;
    boost::filesystem::path outfile;
    boost::filesystem::path genome;
    std::vector<boost::filesystem::path> files;
  };


  template<typename TConfigStruct>
  inline int wallyRun(TConfigStruct& c) {
#ifdef PROFILE
    ProfilerStart("wally.prof");
#endif

    cv::Mat bg( c.height, c.width, CV_8UC3, cv::Scalar(255, 255, 255));

    // Tracks
    int32_t maxTracks = c.height / c.tlheight;
    std::vector<int32_t> taken(maxTracks, -2000000);
    
    // Open file handles
    typedef std::vector<samFile*> TSamFile;
    typedef std::vector<hts_idx_t*> TIndex;
    TSamFile samfile(c.files.size());
    TIndex idx(c.files.size());
    for(unsigned int file_c = 0; file_c < c.files.size(); ++file_c) {
      samfile[file_c] = sam_open(c.files[file_c].string().c_str(), "r");
      hts_set_fai_filename(samfile[file_c], c.genome.string().c_str());
      idx[file_c] = sam_index_load(samfile[file_c], c.files[file_c].string().c_str());
    }
    bam_hdr_t* hdr = sam_hdr_read(samfile[0]);

    // Parse BAM files
    boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
    std::cout << '[' << boost::posix_time::to_simple_string(now) << "] " << "Parsing BAMs" << std::endl;
    faidx_t* fai = fai_load(c.genome.string().c_str());

    // Split region
    uint32_t rgbeg = 17383;
    uint32_t rgend = 17456;
    std::string chrName("NA");
    if (!parseRegion(c.regionStr, chrName, rgbeg, rgend)) {
      std::cerr << "Invalid region specified: " << c.regionStr << std::endl;
      return 1;
    }
    int32_t tid = bam_name2id(hdr, chrName.c_str());
    if (tid < 0) {
      std::cerr << "Chromosome does not exist in BAM file: " << chrName << std::endl;
      return 1;
    }

	
    // Iterate files
    int32_t rgsize = rgend - rgbeg;
    for(unsigned int file_c = 0; file_c < c.files.size(); ++file_c) {
      // Read alignments
      hts_itr_t* iter = sam_itr_queryi(idx[file_c], tid, rgbeg, rgend);	
      bam1_t* rec = bam_init1();
      while (sam_itr_next(samfile[file_c], iter, rec) >= 0) {
	if (rec->core.flag & (BAM_FQCFAIL | BAM_FDUP | BAM_FUNMAP | BAM_FSECONDARY | BAM_FSUPPLEMENTARY)) continue;
	if ((rec->core.qual < c.minMapQual) || (rec->core.tid<0)) continue;

	/*
	uint32_t rp = 0; // reference pointer
	uint32_t sp = 0; // sequence pointer
	// Parse the CIGAR
	uint32_t* cigar = bam_get_cigar(rec);
	for (std::size_t i = 0; i < rec->core.n_cigar; ++i) {
	  if ((bam_cigar_op(cigar[i]) == BAM_CMATCH) || (bam_cigar_op(cigar[i]) == BAM_CEQUAL) || (bam_cigar_op(cigar[i]) == BAM_CDIFF)) {
	    // match or mismatch
	    for(std::size_t k = 0; k<bam_cigar_oplen(cigar[i]);++k) {
	      if (rec->core.l_qseq) {
		if (sequence[sp] == refslice[rp]) ++itRg->second.bc.matchCount;
	      } else {
		if (bam_cigar_op(cigar[i]) == BAM_CEQUAL) ++itRg->second.bc.matchCount;
		else if (bam_cigar_op(cigar[i]) == BAM_CDIFF) ++itRg->second.bc.mismatchCount;
	      }
	      // Count bp-level coverage
	      if (itRg->second.bc.cov[rec->core.pos + rp] < itRg->second.bc.maxCoverage) ++itRg->second.bc.cov[rec->core.pos + rp];
	      ++sp;
	      ++rp;
	    }
	  } else if (bam_cigar_op(cigar[i]) == BAM_CDEL) {
	    ++itRg->second.bc.delCount;
	    if (rec->core.l_qseq) ++itRg->second.bc.delHomACGTN[homopolymerContext(sequence, sp, 3)];
	    if (bam_cigar_oplen(cigar[i]) < itRg->second.bc.maxIndelSize) ++itRg->second.bc.delSize[bam_cigar_oplen(cigar[i])];
	    else ++itRg->second.bc.delSize[itRg->second.bc.maxIndelSize];
	    rp += bam_cigar_oplen(cigar[i]);
	  } else if (bam_cigar_op(cigar[i]) == BAM_CINS) {
	    ++itRg->second.bc.insCount;
	    if (rec->core.l_qseq) ++itRg->second.bc.insHomACGTN[homopolymerContext(sequence, sp, 3)];
	    if (bam_cigar_oplen(cigar[i]) < itRg->second.bc.maxIndelSize) ++itRg->second.bc.insSize[bam_cigar_oplen(cigar[i])];
	    else ++itRg->second.bc.insSize[itRg->second.bc.maxIndelSize];
	    sp += bam_cigar_oplen(cigar[i]);
	  } else if (bam_cigar_op(cigar[i]) == BAM_CSOFT_CLIP) {
	    if (!softClippedOnce) {
	      ++itRg->second.bc.softClipCount;
	      softClippedOnce = true;
	    }
	    sp += bam_cigar_oplen(cigar[i]);
	  } else if(bam_cigar_op(cigar[i]) == BAM_CHARD_CLIP) {
	    if (!hardClippedOnce) {
	      ++itRg->second.bc.hardClipCount;
	      hardClippedOnce = true;
	    }
	  } else if (bam_cigar_op(cigar[i]) == BAM_CREF_SKIP) {
	    if (!spliced) {
	      ++itRg->second.rc.spliced;
	      spliced = true;
	    }
	    rp += bam_cigar_oplen(cigar[i]);
	  } else {
	    std::cerr << "Unknown Cigar options" << std::endl;
	    return 1;
	  }
	}
	*/
	

	int32_t px = pixelX(c.width, rgsize, (rec->core.pos - rgbeg));
	int32_t pxend = pixelX(c.width, rgsize, (rec->core.pos + alignmentLength(rec) - rgbeg));

	// Search empty track
	for(uint32_t i = 0; i < taken.size(); ++i) {
	  if (taken[i] + 1 < px) {
	    drawRead(bg, px, i * c.tlheight, pxend - px, c.rdheight, (rec->core.flag & BAM_FREVERSE));
	    taken[i] = pxend;
	    break;
	  }
	}
      }
      bam_destroy1(rec);
      hts_itr_destroy(iter);
    }

    // Clean-up
    fai_destroy(fai);
    bam_hdr_destroy(hdr);
    for(unsigned int file_c = 0; file_c < c.files.size(); ++file_c) {
      hts_idx_destroy(idx[file_c]);
      sam_close(samfile[file_c]);
    }	
    

    std::string str("title");
    cv::imwrite("bg.jpg", bg);
    //cv::imshow(str.c_str(), bg);
    cv::waitKey(0);

#ifdef PROFILE
    ProfilerStop();
#endif
  
    // End
    now = boost::posix_time::second_clock::local_time();
    std::cout << '[' << boost::posix_time::to_simple_string(now) << "] Done." << std::endl;;
    return 0;
  }


  int region(int argc, char **argv) {
    Config c;
    c.tlheight = 10;
    c.rdheight = 8;
    
    // Define generic options
    std::string svtype;
    boost::program_options::options_description generic("Generic options");
    generic.add_options()
      ("help,?", "show help message")
      ("genome,g", boost::program_options::value<boost::filesystem::path>(&c.genome), "genome fasta file")
      ("outfile,o", boost::program_options::value<boost::filesystem::path>(&c.outfile)->default_value("region.png"), "output file")
      ;
    
    boost::program_options::options_description disc("Read selection options");
    disc.add_options()
      ("map-qual,q", boost::program_options::value<uint32_t>(&c.minMapQual)->default_value(1), "min. mapping quality")
      ("max-cov,m", boost::program_options::value<uint32_t>(&c.maxCov)->default_value(100), "max. coverage to display")
      ("region,r", boost::program_options::value<std::string>(&c.regionStr)->default_value("chrA:35-78"), "region to display")
      ;
    
    boost::program_options::options_description geno("Graphics options");
    geno.add_options()
      ("width,x", boost::program_options::value<uint32_t>(&c.width)->default_value(2048), "width of the plot")
      ("height,y", boost::program_options::value<uint32_t>(&c.height)->default_value(1024), "height of the plot")
      ;

    // Define hidden options
    boost::program_options::options_description hidden("Hidden options");
    hidden.add_options()
      ("input-file", boost::program_options::value< std::vector<boost::filesystem::path> >(&c.files), "input file")
      ;
    
    boost::program_options::positional_options_description pos_args;
    pos_args.add("input-file", -1);
    
    // Set the visibility
    boost::program_options::options_description cmdline_options;
    cmdline_options.add(generic).add(disc).add(geno).add(hidden);
    boost::program_options::options_description visible_options;
    visible_options.add(generic).add(disc).add(geno);
    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(cmdline_options).positional(pos_args).run(), vm);
    boost::program_options::notify(vm);
    

    // Check command line arguments
    if ((vm.count("help")) || (!vm.count("input-file")) || (!vm.count("genome"))) { 
      std::cout << std::endl;
      std::cout << "Usage: wally " << argv[0] << " [OPTIONS] -g <ref.fa> <sample1.sort.bam> <sample2.sort.bam> ..." << std::endl;
      std::cout << visible_options << "\n";
      return 0;
    }

    // Show cmd
    boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
    std::cout << '[' << boost::posix_time::to_simple_string(now) << "] ";
    std::cout << "wally ";
    for(int i=0; i<argc; ++i) { std::cout << argv[i] << ' '; }
    std::cout << std::endl;
    
    return wallyRun(c);
  }

}

#endif
