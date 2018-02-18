/** @file SequenceConverter.h */
#ifndef SEQUENCE_CONVERTER_H
#define SEQUENCE_CONVERTER_H

#include <iostream>

#include "Orchestra/Legacy/Pfile.h"

// ISMRMRD
#include "ismrmrd/ismrmrd.h"

namespace OxToIsmrmrd {

#define PLUGIN_DEBUG(v)
enum { PLUGIN_FAILURE = -1, PLUGIN_SUCCESS = 1 };

  class SequenceConverter
  {
  public:
    SequenceConverter() { }
    virtual ~SequenceConverter() { }
    
    /**
     * Create the ISMRMRD acquisitions corresponding to a given view in memory
     *
     * @param pfile Orchestra Pfile object
     * @param view_num View number
     * @returns vector of ISMRMRD::Acquisitions
     */
    virtual std::vector<ISMRMRD::Acquisition> getAcquisitions(GERecon::Legacy::Pfile* pfile,
                                                              unsigned int view_num) = 0;

    virtual boost::shared_ptr<ISMRMRD::NDArray<complex_float_t> > getKSpaceMatrix(
      GERecon::Legacy::Pfile* pfile,
      unsigned int i_echo,
      unsigned int i_phase) = 0;
  };


// This MACRO goes in the Sequence header file
#define SEQUENCE_CONVERTER_DECLARE(SEQ)                 \
  SEQ () : OxToIsmrmrd::SequenceConverter() {}

// This MACRO goes at the end of the Sequence source file
#define SEQUENCE_CONVERTER_FACTORY_DECLARE(SEQ)             \
                                                            \
  extern "C" OxToIsmrmrd::SequenceConverter * make_##SEQ () \
  {                                                         \
    return new SEQ();                                       \
  }                                                         \
                                                            \
  extern "C" void destroy_##SEQ (OxToIsmrmrd::SequenceConverter *s)     \
  {                                                                     \
    delete s;                                                           \
  }

} // namespace OxToIsmrmrd

#endif /* SEQUENCE_CONVERTER_H */
