/** @file GERawConverter.h */
#ifndef GE_RAW_CONVERTER_H
#define GE_RAW_CONVERTER_H

#include <fstream>

// ISMRMRD
#include "ismrmrd/ismrmrd.h"
#include "ismrmrd/dataset.h"
#include "ismrmrd/xml.h"

// Orchestra
#include "Orchestra/Legacy/Pfile.h"
#include "Orchestra/Common/ScanArchive.h"
#include "Orchestra/Common/DownloadData.h"
#include "Orchestra/Control/ProcessingControl.h"

namespace GeToIsmrmrd {

  struct logstream {
    logstream(bool enable) : enabled(enable) {}
    bool enabled;
  };

  template <typename T>
    inline logstream& operator<<(logstream& s, T const& v)
  {
    if (s.enabled) { std::clog << v; }
    return s;
  }

  inline logstream& operator<<(logstream& s, std::ostream& (*f)(std::ostream&))
  {
    if (s.enabled) { f(std::clog); }
    return s;
  }


  class GERawConverter
  {
  public:
    GERawConverter(const std::string& filepath, bool logging=false);

    std::string getIsmrmrdXMLHeader();
    size_t appendNoiseInformation(ISMRMRD::Dataset& d);
    size_t appendAcquisitions(ISMRMRD::Dataset& d);

    std::string getReconConfigName(void);
    void setRDS(bool);

  private:
    GERawConverter(const GERawConverter& other);
    GERawConverter& operator=(const GERawConverter& other);

    ISMRMRD::IsmrmrdHeader lxDownloadDataToIsmrmrdHeader();
    size_t appendImagesFromPfile(ISMRMRD::Dataset& d);
    size_t appendAcquisitionsFromPfile(ISMRMRD::Dataset& d);
    size_t appendAcquisitionsFromArchive(ISMRMRD::Dataset& d);

    bool m_isScanArchive;
    bool m_isRDS;
    GERecon::Legacy::PfilePointer m_pfile;
    GERecon::ScanArchivePointer m_scanArchive;
    GERecon::DownloadDataPointer m_downloadDataPtr;
    GERecon::Control::ProcessingControlPointer m_processingControl;

    logstream m_log;
  };

} // namespace GeToIsmrmrd

#endif  // GE_RAW_CONVERTER_H
