//---------------------------------------------------------
// Copyright 2015 Ontario Institute for Cancer Research
// Written by Jared Simpson (jared.simpson@oicr.on.ca)
//---------------------------------------------------------
//
// nanopolish_poremodel -- Representation of the Oxford
// Nanopore sequencing model, as described in a FAST5 file
//
#include "nanopolish_poremodel.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <bits/stl_algo.h>
#include "../fast5/src/fast5.hpp"

void PoreModel::bake_gaussian_parameters()
{
    scaled_params.resize(states.size());

    for(int i = 0; i < states.size(); ++i) {

        // these functions are provided by ONT
        scaled_params[i].mean = states[i].level_mean * scale + shift;
        scaled_params[i].stdv = states[i].level_stdv * var;
        scaled_params[i].log_stdv = log(scaled_params[i].stdv); // pre-computed for efficiency

        // These are not used, for now
        //scaled_state[i].sd_mean = state[i].sd_mean * scale_sd;
        //scaled_state[i].sd_stdv = state[i].sd_stdv * sqrt(pow(scale_sd, 3.0) / var_sd);
    }
    is_scaled = true;
}

PoreModel::PoreModel(const std::string filename, const Alphabet &alphabet) 
{
    std::ifstream model_reader(filename);
    std::string model_line;

    bool firstKmer = true;
    int ninserted = 0;

    while (getline(model_reader, model_line)) {
        std::stringstream parser(model_line);

        // Extract the model name from the header
        if (model_line.find("#model_name") != std::string::npos) {
            std::string dummy;
            parser >> dummy >> name;
        }

        // skip the rest of the header
        if (model_line[0] == '#' || model_line.find("kmer") == 0) {
            continue;
        }

        std::string kmer;
        PoreModelStateParams params;
        parser >> kmer >> params.level_mean >> params.level_stdv >> params.sd_mean >> params.sd_stdv;

        if (firstKmer) {
            k = kmer.length();
            states.resize( alphabet.get_num_strings(k) );

            firstKmer = false;
        }

        states[ alphabet.kmer_rank(kmer.c_str(), k) ] = params;
        ninserted++;
    }

    assert( ninserted == states.size() );
}

PoreModel::PoreModel(fast5::File *f_p, const size_t strand, const Alphabet &alphabet) 
{

    std::vector<fast5::Model_Entry> model = f_p->get_model(strand);
    k = (uint32_t) strlen(model[0].kmer);

    states.resize( alphabet.get_num_strings(k) );
    assert(states.size() == model.size());

    // Copy into the pore model for this read
    for(size_t mi = 0; mi < model.size(); ++mi) {
        const fast5::Model_Entry& curr = model[mi];

        size_t rank = alphabet.kmer_rank(curr.kmer, k);
        states[rank] = { static_cast<float>(curr.level_mean),
                         static_cast<float>(curr.level_stdv),
                         static_cast<float>(curr.sd_mean),
                         static_cast<float>(curr.sd_stdv) };
    }

    // Load the scaling parameters for the pore model
    fast5::Model_Parameters params = f_p->get_model_parameters(strand);
    drift = params.drift;
    scale = params.scale;
    scale_sd = params.scale_sd;
    shift = params.shift;
    var = params.var;
    var_sd = params.var_sd;

    // apply shift/scale transformation to the pore model states
    bake_gaussian_parameters();

    // Read and shorten the model name
    std::string temp_name = f_p->get_model_file(strand);
    std::string leader = "/opt/chimaera/model/";

    size_t lp = temp_name.find(leader);
    // leader not found
    if(lp == std::string::npos) {
        name = temp_name;
    } else {
        name = temp_name.substr(leader.size());
    }

    std::replace(name.begin(), name.end(), '/', '_');
}

void PoreModel::write(const std::string filename, const Alphabet& alphabet, const std::string modelname) 
{
    std::string outmodelname=modelname;
    if (modelname.empty())
        outmodelname = name;

    std::ofstream writer(filename);
    writer << "#model_name\t" << outmodelname << std::endl;

    std::string curr_kmer(k,alphabet.base(0));
    for(size_t ki = 0; ki < states.size(); ++ki) {
        writer << curr_kmer << "\t" << states[ki].level_mean << "\t" << states[ki].level_stdv << "\t"
               << states[ki].sd_mean << "\t" << states[ki].sd_stdv << std::endl;
        alphabet.lexicographic_next(curr_kmer);
    }
    writer.close();
}

void PoreModel::update_states( const PoreModel &other ) 
{
    update_states( other.states );
}

void PoreModel::update_states( const std::vector<PoreModelStateParams> &otherstates ) 
{
    states = otherstates;
    if (is_scaled) {
        bake_gaussian_parameters();
    }
}
