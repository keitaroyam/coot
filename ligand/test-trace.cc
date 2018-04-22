/* ligand/test-trace.cc
 * 
 * Copyright 2016 by Medical Research Council
 * Author: Paul Emsley
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifndef _MSC_VER
#include <unistd.h> // for getopt(3)
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef __GNU_LIBRARY__
#include "compat/coot-getopt.h"
#else
#define __GNU_LIBRARY__
#include "compat/coot-getopt.h"
#undef __GNU_LIBRARY__
#endif

#include <stdlib.h>

#include <clipper/ccp4/ccp4_map_io.h>

#include "../utils/coot-utils.hh"
#include "../geometry/residue-and-atom-specs.hh"
#include "../coot-utils/coot-coord-utils.hh"
#include "../coot-utils/coot-map-utils.hh"
#include "multi-peptide.hh"
#include "trace.hh"

void show_usage(std::string pname) {
   std::cout << "Usage: " << pname
	     << " --pdbin pdb-in-filename" << " --hklin mtz-file-name"
	     << " --f f_col_label"
	     << " --phi phi_col_label"
	     << " --pdbout output-file-name"
	     << "\n"
	     << "   --mapin ccp4-map-name can be used"
	     << " instead of --hklin --f --phi\n";
   std::cout << "   where pdbin is used for seeding (debugging option).\n"
	     << "   pdbout is the output model file name.\n";
}

int main(int argc, char **argv) {

   if (argc < 2) {
      show_usage(argv[0]);
   } else {
      const char *optstr = "i:h:f:p:o:s:e:c:m";
      struct option long_options[] = {
	 {"pdbin",  1, 0, 0},
	 {"hklin",  1, 0, 0},
	 {"f",      1, 0, 0},
	 {"phi",    1, 0, 0},
	 {"pdbout", 1, 0, 0},
	 {"mapin",  1, 0, 0},
	 {"chain-id", 1, 0, 0},
	 {"res-no",   1, 0, 0},
	 {"sequence-file",   1, 0, 0},
	 {0, 0, 0, 0}
      };

      std::string pdb_file_name;
      std::string map_file_name;
      std::string output_pdb;
      std::string hklin_file_name;
      std::string f_col_label;
      std::string phi_col_label;
      std::string chain_id;
      std::string res_no_str;
      std::string sequence_file_name;
      bool self_seed = true;
      bool debugging = false;

      int ch;
      int option_index = 0;
      while ( -1 !=
	      (ch = getopt_long(argc, argv, optstr, long_options, &option_index))) {

	 switch(ch) {

	 case 0:
	    if (optarg) {
	       std::string arg_str = long_options[option_index].name;

	       if (arg_str == "pdbin") {
		  pdb_file_name = optarg;
	       }
	       if (arg_str == "mapin") {
		  map_file_name = optarg;
	       }
	       if (arg_str == "pdbout") {
		  output_pdb = optarg;
	       }
	       if (arg_str == "hklin") {
		  hklin_file_name = optarg;
	       }
	       if (arg_str == "f") {
		  f_col_label = optarg;
	       }
	       if (arg_str == "phi") {
		  phi_col_label = optarg;
	       }
	       if (arg_str == "chain-id") {
		  chain_id = optarg;
	       }
	       if (arg_str == "res-no") {
		  res_no_str = optarg;
	       }
	       if (arg_str == "sequence-file") {
		  sequence_file_name = arg_str;
	       }
	       if (arg_str == "debug") {
		  debugging = true;
	       }
	    }
	 }
      }

      clipper::Xmap<float> xmap;
      if (! f_col_label.empty()) {
	 if (! phi_col_label.empty()) {
	    bool use_weights = false;
	    bool is_diff_map = false;
	    bool stat = coot::util::map_fill_from_mtz(&xmap, hklin_file_name,
						      f_col_label, phi_col_label, "",
						      use_weights, is_diff_map);
	 }
      }
      if (coot::file_exists(map_file_name)) {

	 try {
	    std::cout << "reading map " << map_file_name << std::endl;
	    clipper::CCP4MAPfile file;
	    file.open_read(map_file_name);
	    file.import_xmap(xmap);
	    file.close_read();
	 }
	 // problem reading the map, perhaps?
	 //
	 catch (const clipper::Message_fatal &mess) {
	    std::cout << "ERROR:: " << mess.text() << std::endl;
	 }
      }

      if (! xmap.is_null()) {

	 coot::protein_geometry geom;
	 geom.set_verbose(0);
	 geom.init_standard();
	 geom.remove_planar_peptide_restraint();
	 std::pair<float, float> mv = coot::util::mean_and_variance(xmap);

	 coot::trace t(xmap);

	 // This (and the use of set_cell() and set_spacegroup() later) is inelegant.
	 float acell[6];
	 acell[0] = xmap.cell().descr().a();
	 acell[1] = xmap.cell().descr().b();
	 acell[2] = xmap.cell().descr().c();
	 acell[3] = clipper::Util::rad2d(xmap.cell().descr().alpha());
	 acell[4] = clipper::Util::rad2d(xmap.cell().descr().beta());
	 acell[5] = clipper::Util::rad2d(xmap.cell().descr().gamma());
	 std::string spacegroup_str_hm = xmap.spacegroup().symbol_hm();

	 if (pdb_file_name.empty()) {

	    // self seed

	    std::vector<coot::minimol::fragment> seeds = t.make_seeds();
	    std::cout << "INFO:: Made " << seeds.size() << " seeds " << std::endl;

	    for (std::size_t iseed=0; iseed<seeds.size(); iseed++) {
	       coot::minimol::molecule mmm(seeds[iseed]);
	       mmdb::Manager *mol = mmm.pcmmdbmanager();
	       mmdb::Residue *r = coot::util::get_residue(coot::residue_spec_t("A", 2, ""), mol);
	       mmdb::Residue *r_prev = coot::util::get_previous_residue(r, mol);
	       float b_fact = 30.0;
	       int n_trials = 20000; // 20000 is a reasonable minimal number
	       coot::minimol::fragment fC =
		  coot::multi_build_terminal_residue_addition_forwards_2018(r, r_prev, "A",
									    b_fact, n_trials,
									    geom, xmap, mv,
									    debugging);
	       std::string file_name = "trace-frag-forward-build-from-seed-";
	       file_name += coot::util::int_to_string(iseed);
	       file_name += ".pdb";
	       coot::minimol::molecule mmm_out(fC);
	       // add cell from map
	       mmm_out.set_cell(acell);
	       mmm_out.set_spacegroup(spacegroup_str_hm);
	       mmm_out.write_file(file_name, 10);

	    }

	 } else {

	    // seed from a residue in a PDB file (for devel/debug/testing)

	    if (coot::file_exists(pdb_file_name)) {
	       mmdb::Manager *mol = new mmdb::Manager;
	       std::cout << "Reading coordinate file: " << pdb_file_name.c_str() << "\n";
	       mmdb::ERROR_CODE err = mol->ReadCoorFile(pdb_file_name.c_str());
	       if (err) {
		  std::cout << "There was an error reading " << pdb_file_name.c_str() << ". \n";
		  std::cout << "ERROR " << err << " READ: "
			    << mmdb::GetErrorDescription(err) << std::endl;
		  delete mol;
	       } else {

		  if (! chain_id.empty()) {
		     int res_no = -999;
		     if (! res_no_str.empty()) {
			try {
			   int r = coot::util::string_to_int(res_no_str);
			   res_no = r;
			}
			catch (const std::runtime_error &rte) {
			   std::cout << "ERROR:: res-no extraction: " << rte.what() << std::endl;
			}
		     }
		     if (res_no > -999) {
			self_seed = false;
			coot::residue_spec_t spec(chain_id, res_no, "");
			mmdb::Residue *r = coot::util::get_residue(spec, mol);
			if (r) {
			   mmdb::Residue *r_prev = coot::util::previous_residue(r);
			   float b_fact = 30.0;
			   int n_trials = 20000; // 20000 is a reasonable minimal number
			   coot::minimol::fragment fC =
			      coot::multi_build_terminal_residue_addition_forwards_2018(r, r_prev, "A",
											b_fact, n_trials,
											geom, xmap, mv,
											debugging);
			   std::string file_name = "trace-frag-forwards-build.pdb";
			   coot::minimol::molecule mmm(fC);
			   mmm.write_file(file_name, 10);
			}
		     }
		  }
	       }
	    }
	 }
      }
   }

   return 0;
}
