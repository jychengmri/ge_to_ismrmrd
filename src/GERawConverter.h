/** @file GERawConverter.h */
#ifndef GE_RAW_CONVERTER_H
#define GE_RAW_CONVERTER_H

#include <fstream>

// ISMRMRD
#include "ismrmrd/ismrmrd.h"
#include "ismrmrd/dataset.h"

// Orchestra
#include "Orchestra/Legacy/Pfile.h"
#include "Orchestra/Common/ScanArchive.h"
#include "Orchestra/Common/DownloadData.h"
#include "Orchestra/Control/ProcessingControl.h"

// Local
#include "SequenceConverter.h"

// Libxml2 forward declarations
struct _xmlDoc;
struct _xmlNode;

namespace OxToIsmrmrd {

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
    GERawConverter(const std::string& pfilepath, bool logging=false);
    //GERawConverter(void *hdr_loc, bool logging=false);

    void useStylesheetFilename(const std::string& filename);
    void useStylesheetStream(std::ifstream& stream);
    void useStylesheetString(const std::string& sheet);

    void useConfigFilename(const std::string& filename);
    void useConfigStream(std::ifstream& stream);
    void useConfigString(const std::string& config);

    std::string getIsmrmrdXMLHeader();

    std::vector<ISMRMRD::Acquisition> getAcquisitions(unsigned int view_num);
    boost::shared_ptr<ISMRMRD::NDArray<complex_float_t> > getKSpaceMatrix(unsigned int i_echo,
                                                                          unsigned int i_phase);

    void appendAcquisitions(ISMRMRD::Dataset& d);

    std::string getReconConfigName(void);
    unsigned int getNumViews(void);
    unsigned int getNumEchoes(void);
    unsigned int getNumPhases(void);
    void setRDS(void);

  private:
    // Non-copyable
    GERawConverter(const GERawConverter& other);
    GERawConverter& operator=(const GERawConverter& other);

    bool validateConfig(std::shared_ptr<struct _xmlDoc> config_doc);
    bool trySequenceMapping(std::shared_ptr<struct _xmlDoc> doc, struct _xmlNode* mapping);

    std::string m_psdname;
    std::string m_recon_config;
    std::string m_stylesheet;

    bool m_isScanArchive;
    GERecon::Legacy::PfilePointer m_pfile;
    GERecon::ScanArchivePointer m_scanArchive;
    GERecon::DownloadDataPointer m_downloadDataPtr;
    GERecon::Control::ProcessingControlPointer m_processingControl;
    std::shared_ptr<SequenceConverter> m_converter;

    logstream log_;
  };

} // namespace OxToIsmrmrd

#endif  // GE_RAW_CONVERTER_H
