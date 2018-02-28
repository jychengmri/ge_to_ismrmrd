#include <cstdio>

// Boost
#include <boost/program_options.hpp>

// ISMRMRD
#include "ismrmrd/ismrmrd.h"
#include "ismrmrd/dataset.h"

// Orchestra
#include "System/Utilities/Main.h"

// GE
#include "GERawConverter.h"

namespace po = boost::program_options;

int main (int argc, char *argv[])
{
  GESystem::Main(argc, argv);
  std::string classname, stylesheet, inputFileName, outputFileName;
  std::string usage("ox2ismrmrd [options] <input file>");

  po::options_description basic("Basic Options");
  basic.add_options()
    ("help,h", "print help message")
    ("verbose,v", "enable verbose mode")
    ("output,o", po::value<std::string>(&outputFileName)->default_value("output.h5"), "output HDF5 file")
    //("rdsfile,r", "P-File from the RDS client")
    ("string,s", "only print the HDF5 XML header")
    ;

  po::options_description input("Input Options");
  input.add_options()
    ("input,i", po::value<std::string>(&inputFileName), "input file (InputFileName or ScanArchive)")
    ;

  po::options_description all_options("Options");
  all_options.add(basic).add(input);

  po::options_description visible_options("Options");
  visible_options.add(basic);

  po::positional_options_description positionals;
  positionals.add("input", 1);

  po::variables_map vm;
  try {
    po::store(po::command_line_parser(argc, argv).options(all_options).positional(positionals).run(), vm);

    po::notify(vm);
  } catch (const po::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
    std::cerr << usage << std::endl << visible_options << std::endl;
    return EXIT_FAILURE;
  }

  if (vm.count("help")) {
    std::cerr << usage << std::endl << visible_options << std::endl;
    return EXIT_SUCCESS;
  }

  if (inputFileName.size() == 0) {
    std::cerr << usage << std::endl;
    return EXIT_FAILURE;
  }

  bool verbose = false;
  if (vm.count("verbose")) {
    verbose = true;
  }

  // Create a new Converter
  std::shared_ptr<GeToIsmrmrd::GERawConverter> converter;
  try {
    converter = std::make_shared<GeToIsmrmrd::GERawConverter>(inputFileName, verbose);
  } catch (const std::exception& e) {
    std::cerr << "Failed to instantiate converter: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  // Get the ISMRMRD Header String
  std::string xml_header;
  try {
    xml_header = converter->getIsmrmrdXMLHeader();
  } catch (const std::exception& e) {
    std::cerr << "Failed to get header string: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  if (xml_header.size() == 0) {
    std::cerr << "Empty ISMRMRD XML header... Exiting" << std::endl;
    return EXIT_FAILURE;
  }

  // if the user requested only a dump of the XML header:
  if (vm.count("string")) {
    std::cout << xml_header << std::endl;
    return EXIT_SUCCESS;
  }

  // create hdf5 file
  ISMRMRD::Dataset d(outputFileName.c_str(), "dataset", true);

  // write the ISMRMRD header to the dataset
  d.writeHeader(xml_header);

  // if the user has specified that this is an RDS file:
  // if (vm.count("rdsfile")) {
  //   converter->setRDS();
  // }

  // Append data from file
  converter->appendAcquisitions(d);

  if (verbose)
    std::cout << "Done" << std::endl;

  return EXIT_SUCCESS;
}
