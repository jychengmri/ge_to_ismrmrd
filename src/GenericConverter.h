
/** @file GenericConverter.h */
#ifndef GENERIC_CONVERTER_H
#define GENERIC_CONVERTER_H

#include "SequenceConverter.h"

namespace OxToIsmrmrd {

  class GenericConverter: public SequenceConverter
  {
  public:
    SEQUENCE_CONVERTER_DECLARE(GenericConverter)
      virtual std::vector<ISMRMRD::Acquisition> getAcquisitions(
        GERecon::Legacy::Pfile* pfile, unsigned int acq_mode);
    
    virtual boost::shared_ptr<ISMRMRD::NDArray<complex_float_t> > getKSpaceMatrix(
      GERecon::Legacy::Pfile* pfile, unsigned int i_echo, unsigned int i_phase);

  protected:
    virtual int get_view_idx(GERecon::Legacy::Pfile *pfile,
                             unsigned int view_num, ISMRMRD::EncodingCounters &idx);
  };

} // namespace OxToIsmrmrd

#endif /* GENERIC_CONVERTER_H */

