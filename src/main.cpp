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
    ("stylesheet,x", po::value<std::string>(&stylesheet)->default_value(""), "XSL stylesheet file")
    ("output,o", po::value<std::string>(&outputFileName)->default_value("testdata.h5"), "output HDF5 file")
    ("rdsfile,r", "P-File from the RDS client")
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
  std::shared_ptr<OxToIsmrmrd::GERawConverter> converter;
  try {
    converter = std::make_shared<OxToIsmrmrd::GERawConverter>(inputFileName, verbose);
  } catch (const std::exception& e) {
    std::cerr << "Failed to instantiate converter: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  // Override stylesheet if specified
  if (stylesheet.size() > 0) {
    try {
      converter->useStylesheetFilename(stylesheet);
    } catch (const std::exception& e) {
      std::cerr << "Failed to override stylesheet: " << e.what() << std::endl;
      //return EXIT_FAILURE;
    }
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
  if (vm.count("rdsfile")) {
    converter->setRDS();
  }

  converter->appendAcquisitions(d);

  /*
  unsigned int numPhases = converter->getNumPhases();
  unsigned int numEchoes = converter->getNumEchoes();
  char kSpaceName [128];
  for (unsigned int i_phase = 0; i_phase < numPhases; i_phase ++)
    for (unsigned int i_echo = 0; i_echo < numEchoes; i_echo ++) {
      if (numPhases > 1)
        std::sprintf(kSpaceName, "kSpaceMatrix_phase%02d", i_phase);
      else
        std::sprintf(kSpaceName, "kSpaceMatrix");
      boost::shared_ptr<ISMRMRD::NDArray<complex_float_t> > kSpaceMatrix = converter->getKSpaceMatrix(i_echo, i_phase);
      d.appendNDArray(kSpaceName, *kSpaceMatrix);
    }
  */
   /*
  unsigned int nviews = converter->getNumViews();
  std::cout << "Num views = " << nviews << std::endl;
  // for (unsigned int view_num = 0; view_num < nviews; view_num++) {

  // get the acquisitions corresponing to this view
  std::vector<ISMRMRD::Acquisition> acqs = converter->getAcquisitions(0);

  std::cout << "Number of acquisitions stored in HDF5 file is " << acqs.size() << std::endl;
  // add these acquisitions to the hdf5 dataset
  for (int n = 0; n < (int) acqs.size(); n++) {
    d.appendAcquisition(acqs.at(n));
  }
  // }
  */
  //std::cout << "Swedished!" << std::endl;

  return EXIT_SUCCESS;
}
