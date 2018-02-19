
/** @file GERawConverter.cpp */
#include <iostream>
#include <stdexcept>

#include <libxml/xmlschemas.h>
#include <libxslt/xslt.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

#include <Orchestra/Acquisition/ControlPacket.h>
#include <Orchestra/Acquisition/ControlTypes.h>
#include <Orchestra/Acquisition/Core/ArchiveStorage.h>
#include <Orchestra/Acquisition/DataTypes.h>
#include <Orchestra/Acquisition/FrameControl.h>

#include <Orchestra/Legacy/LegacyImageDB.h>
#include <Orchestra/Legacy/LegacyRdbm.h>
#include <Orchestra/Legacy/LxControlSource.h>
#include <Orchestra/Legacy/LegacyForwardDeclarations.h>
#include <Orchestra/Legacy/DicomSeries.h>
#include <Orchestra/Legacy/PoolHeader/HeaderMap.h>

#include <System/Utilities/CommonTypes.h>
#include <System/Utilities/StructMap.h>
#include <System/Utilities/StructEntry.h>

#include <Orchestra/Common/DataSampleType.h>
#include <Orchestra/Common/ArchiveHeader.h>
#include <Orchestra/Common/PrepData.h>
#include <Orchestra/Common/ImageCorners.h>
#include <Orchestra/Common/SliceInfoTable.h>
#include <Orchestra/Common/SliceOrientation.h>

#include <Orchestra/Control/ProcessingControl.h>

#include <Dicom/MR/Image.h>
#include <Dicom/MR/ImageModule.h>
#include <Dicom/MR/PrivateAcquisitionModule.h>
#include <Dicom/Patient.h>
#include <Dicom/PatientModule.h>
#include <Dicom/PatientStudyModule.h>
#include <Dicom/Equipment.h>
#include <Dicom/EquipmentModule.h>
#include <Dicom/ImagePlaneModule.h>

// Local
#include "GERawConverter.h"
#include "XMLWriter.h"
#include "GenericConverter.h"


namespace OxToIsmrmrd {

  const std::string g_schema = "\
<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>            \
<xs:schema xmlns=\"https://github.com/nih-fmrif/GEISMRMRD\"             \
    xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"                       \
    elementFormDefault=\"qualified\"                                    \
    targetNamespace=\"https://github.com/nih-fmrif/GEISMRMRD\">         \
    <xs:element name=\"conversionConfiguration\">                       \
        <xs:complexType>                                                \
            <xs:sequence>                                               \
                <xs:element maxOccurs=\"unbounded\" minOccurs=\"1\"     \
                    name=\"sequenceMapping\" type=\"sequenceMappingType\"/> \
            </xs:sequence>                                              \
        </xs:complexType>                                               \
    </xs:element>                                                       \
    <xs:complexType name=\"sequenceMappingType\">                           \
        <xs:all>                                                        \
            <xs:element name=\"psdname\" type=\"xs:string\"/>           \
            <xs:element name=\"libraryPath\" type=\"xs:string\"/>       \
            <xs:element name=\"className\" type=\"xs:string\"/>         \
            <xs:element name=\"stylesheet\" type=\"xs:string\"/>        \
            <xs:element name=\"reconConfigName\" type=\"xs:string\"/>   \
        </xs:all>                                                       \
    </xs:complexType>                                                   \
</xs:schema>";

  static std::string downloadDataToXML(const GERecon::DownloadDataPointer downloadDataPtr);
  static void lxDownloadDataToXML(XMLWriter* writer,
                                  const GERecon::Legacy::LxDownloadDataPointer lxDownloadDataPtr);
  
  std::string convert_date(const std::string& date_str) {
    if (date_str.length() == 8) {
      return date_str.substr(0, 4) + "-"
        + date_str.substr(5, 2) + "-" + date_str.substr(6, 2);
    } else
      return date_str;
  }
  
  std::string convert_time(const std::string& date_str) {
    if (date_str.length() == 6) {
      return date_str.substr(0, 2) + ":"
        + date_str.substr(2, 2) + ":" + date_str.substr(4, 2);
    } else
      return date_str;
  }

  
  /**
   * Creates a GERawConverter from an ifstream of the PFile header
   *
   * @param fp raw FILE pointer to PFile
   * @throws std::runtime_error if P-File cannot be read
   */
  GERawConverter::GERawConverter(const std::string& pfilepath, bool logging)
    : m_psdname(""),
      m_recon_config(""),
      m_stylesheet(""),
      m_pfile(NULL),
      m_scanArchive(NULL),
      m_downloadDataPtr(NULL),
      m_processingControl(NULL),
      m_converter(NULL),
      log_(logging)
  {
    FILE* fp = NULL;
    if (!(fp = fopen(pfilepath.c_str(), "rb"))) {
      throw std::runtime_error("Failed to open " + pfilepath);
    }

    /*
      m_psdname = ""; // TODO: find PSD Name in Orchestra Pfile class
      log_ << "PSDName: " << m_psdname << std::endl;
    */
    
    if (GERecon::ScanArchive::IsArchiveFilePath(pfilepath)) {
      m_scanArchive = GERecon::ScanArchive::Create(pfilepath, GESystem::Archive::LoadMode);

      m_downloadDataPtr = m_scanArchive->LoadDownloadData();
      auto lxDownloadDataPtr =  boost::dynamic_pointer_cast<GERecon::Legacy::LxDownloadData>(m_downloadDataPtr);
      auto controlSource = boost::make_shared<GERecon::Legacy::LxControlSource>(lxDownloadDataPtr);
      m_processingControl = controlSource->CreateOrchestraProcessingControl();
      
      m_isScanArchive = true;
    }
    else {
      m_pfile = GERecon::Legacy::Pfile::Create(
        pfilepath,
        GERecon::Legacy::Pfile::AllAvailableAcquisitions,
        GERecon::AnonymizationPolicy(GERecon::AnonymizationPolicy::None));

      m_downloadDataPtr = m_pfile->DownloadData();
      m_processingControl = m_pfile->CreateOrchestraProcessingControl();
      
      m_isScanArchive = false;
    }

    m_converter = std::shared_ptr<SequenceConverter>(new GenericConverter());
  } // constructor GERawConverter::GERawConverter()

  
  void GERawConverter::useStylesheetFilename(const std::string& filename)
  {
    log_ << "Loading stylesheet: " << filename << std::endl;
    std::ifstream stream(filename.c_str(), std::ios::binary);
    useStylesheetStream(stream);
  } // function GERawConverter::useStyelsheetFilename
  
  void GERawConverter::useStylesheetStream(std::ifstream& stream)
  {
    stream.seekg(0, std::ios::beg);
    
    std::string sheet((std::istreambuf_iterator<char>(stream)),
                      std::istreambuf_iterator<char>());
    useStylesheetString(sheet);
  }

  void GERawConverter::useStylesheetString(const std::string& sheet)
  {
    m_stylesheet = sheet;
  }

  void GERawConverter::useConfigFilename(const std::string& filename)
  {
    log_ << "Loading configuration: " << filename << std::endl;
    std::ifstream stream(filename.c_str(), std::ios::binary);
    useConfigStream(stream);
  }

  void GERawConverter::useConfigStream(std::ifstream& stream)
  {
    stream.seekg(0, std::ios::beg);
    
    std::string config((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
    useConfigString(config);
  }

  bool GERawConverter::validateConfig(std::shared_ptr<xmlDoc> config_doc)
  {
    log_ << "Validating configuration" << std::endl;
    
    std::shared_ptr<xmlDoc> schema_doc = std::shared_ptr<xmlDoc>(
      xmlParseMemory(g_schema.c_str(), g_schema.size()), xmlFreeDoc);
    if (!schema_doc) {
      throw std::runtime_error("Failed to parse embedded config-file schema");
    }
    
    std::shared_ptr<xmlSchemaParserCtxt> parser_ctx = std::shared_ptr<xmlSchemaParserCtxt>(
      xmlSchemaNewDocParserCtxt(schema_doc.get()), xmlSchemaFreeParserCtxt);
    if (!parser_ctx) {
      throw std::runtime_error("Failed to create schema parser");
    }

    std::shared_ptr<xmlSchema> schema = std::shared_ptr<xmlSchema>(
      xmlSchemaParse(parser_ctx.get()), xmlSchemaFree);
    if (!schema) {
      throw std::runtime_error("Failed to create schema");
    }
    
    std::shared_ptr<xmlSchemaValidCtxt> valid_ctx = std::shared_ptr<xmlSchemaValidCtxt>(
      xmlSchemaNewValidCtxt(schema.get()), xmlSchemaFreeValidCtxt);
    if (!valid_ctx) {
      throw std::runtime_error("Failed to create schema validity context");
    }
    
    // Set error/warning logging functions
    // xmlSchemaSetValidErrors(valid_ctx, errors, warnings, NULL);

    if (xmlSchemaValidateDoc(valid_ctx.get(), config_doc.get()) == 0) {
      return true;
    }
    return false;
  }

  
  /**
   * Validates configuration then loads plugin, stylesheet
   *
   * TODO: Leaks memory if exception thrown
   */
  void GERawConverter::useConfigString(const std::string& config)
  {
    std::string error_message;
    
    std::shared_ptr<xmlDoc> config_doc = std::shared_ptr<xmlDoc>(
      xmlParseMemory(config.c_str(), config.size()), xmlFreeDoc);
    if (!config_doc) {
      throw std::runtime_error("Failed to parse config");
    }
    
    if (!validateConfig(config_doc)) {
      throw std::runtime_error("Invalid configuration");
    }
  
    log_ << "Searching for sequence mapping" << std::endl;
    
    xmlNodePtr cur = xmlDocGetRootElement(config_doc.get());
    if (NULL == cur) {
      throw std::runtime_error("Can't get root element of configuration");
    }
    
    if (xmlStrcmp(cur->name, (const xmlChar *)"conversionConfiguration")) {
      throw std::runtime_error("root element should be \"conversionConfiguration\"");
    }
    
    cur = cur->xmlChildrenNode;
    while (cur != NULL) {
      if (xmlStrcmp(cur->name, (const xmlChar*) "sequenceMapping") == 0) {
        if (trySequenceMapping(config_doc, cur)) {
          break;
        }
      }
      
      cur = cur->next;
    }
  }

  
  /**
   * Attempts to load and use a sequence mapping from an XML config.
   *
   * Returns `true` on success, `false` otherwise.
   */
  bool GERawConverter::trySequenceMapping(std::shared_ptr<xmlDoc> doc, xmlNodePtr mapping)
  {
    xmlNodePtr parameter = mapping->xmlChildrenNode;
    std::string psdname, libpath, classname, stylesheet, reconconfig;
    
    while (parameter != NULL) {
      if (xmlStrcmp(parameter->name, (const xmlChar*)"psdname") == 0) {
        char *tmp = (char*)xmlNodeListGetString(doc.get(), parameter->xmlChildrenNode, 1);
        psdname = std::string(tmp);
        xmlFree(tmp);
      } else if (xmlStrcmp(parameter->name, (const xmlChar*)"libraryPath") == 0) {
        char *tmp = (char*)xmlNodeListGetString(doc.get(), parameter->xmlChildrenNode, 1);
        libpath = std::string(tmp);
        xmlFree(tmp);
      } else if (xmlStrcmp(parameter->name, (const xmlChar*)"className") == 0) {
        char *tmp = (char*)xmlNodeListGetString(doc.get(), parameter->xmlChildrenNode, 1);
        classname = std::string(tmp);
        xmlFree(tmp);
      } else if (xmlStrcmp(parameter->name, (const xmlChar*)"stylesheet") == 0) {
        char *tmp = (char*)xmlNodeListGetString(doc.get(), parameter->xmlChildrenNode, 1);
        stylesheet = std::string(tmp);
        xmlFree(tmp);
      } else if (xmlStrcmp(parameter->name, (const xmlChar*)"reconConfigName") == 0) {
        char *tmp = (char*)xmlNodeListGetString(doc.get(), parameter->xmlChildrenNode, 1);
        reconconfig = std::string(tmp);
        xmlFree(tmp);
      }
      parameter = parameter->next;
    }
    
    return true;
  }

  
  /**
   * Converts the XSD ISMRMRD XML header object into a C++ string
   *
   * @returns string represenatation of ISMRMRD XML header
   * @throws std::runtime_error
   */
  std::string GERawConverter::getIsmrmrdXMLHeader()
  {

    if (m_downloadDataPtr == NULL) {
      throw std::runtime_error("DownloadData not loaded");
    }
    std::string headerXML (downloadDataToXML(m_downloadDataPtr));
    // std::cout << headerXML << std::endl;
    
    xmlSubstituteEntitiesDefault(1);
    xmlLoadExtDtdDefaultValue = 1;
    
    std::shared_ptr<xmlDoc> pfile_doc = std::shared_ptr<xmlDoc>(
      xmlParseMemory(headerXML.c_str(), headerXML.size()), xmlFreeDoc);
    if (!pfile_doc) {
      throw std::runtime_error("Failed to parse P-File XML");
    }

    if (m_stylesheet.size() > 0) {
      log_ << "Applying stylesheet: " << m_stylesheet << std::endl;
      // Normal pointer here because the xsltStylesheet takes ownership
      xmlDocPtr stylesheet_doc = xmlParseMemory(m_stylesheet.c_str(), m_stylesheet.size());
      if (NULL == stylesheet_doc) {
        throw std::runtime_error("Failed to parse stylesheet");
      }
    
      std::shared_ptr<xsltStylesheet> sheet = std::shared_ptr<xsltStylesheet>(
        xsltParseStylesheetDoc(stylesheet_doc), xsltFreeStylesheet);
      if (!sheet) {
        xmlFreeDoc(stylesheet_doc);
        throw std::runtime_error("Failed to parse stylesheet");
      }
    
      const char *params[1] = { NULL };
      std::shared_ptr<xmlDoc> result = std::shared_ptr<xmlDoc>(
        xsltApplyStylesheet(sheet.get(), pfile_doc.get(), params), xmlFreeDoc);
      if (!result) {
        throw std::runtime_error("Failed to apply stylesheet");
      }
    
      xmlChar* output = NULL;
      int len = 0;
      if (xsltSaveResultToString(&output, &len, result.get(), sheet.get()) < 0) {
        throw std::runtime_error("Failed to save converted doc to string");
      }
    
      std::string ismrmrd_header((char*)output, len);
      xmlFree(output);
      return ismrmrd_header;
    }

    return headerXML;
  }


  /**
   * Gets the acquisitions corresponding to a view in memory (P-file).
   *
   * @param view_num View number to get
   * @param vacq Vector of acquisitions
   * @throws std::runtime_error { if plugin fails to copy the data }
   */
  std::vector<ISMRMRD::Acquisition> GERawConverter::getAcquisitions(unsigned int view_num)
  {
    if (m_isScanArchive)
      return std::vector<ISMRMRD::Acquisition>();
    else
      return m_converter->getAcquisitions(m_pfile.get(), view_num);
  }

  
  boost::shared_ptr<ISMRMRD::NDArray<complex_float_t> > GERawConverter::getKSpaceMatrix(
    unsigned int i_echo, unsigned int i_phase)
  {
    if (m_isScanArchive)
      return boost::shared_ptr<ISMRMRD::NDArray<complex_float_t> >();
    else
      return m_converter->getKSpaceMatrix(m_pfile.get(), i_echo, i_phase);
  }

  
  /**
   * Gets the extra field "reconConfig" from the
   * ge-ismrmrd XML configuration. This can be used to
   * add this library to a Gadgetron client
   */
  std::string GERawConverter::getReconConfigName(void)
  {
    return std::string(m_recon_config);
  }

  
  /**
   * Gets the number of views in the pfile
   */
  unsigned int GERawConverter::getNumViews(void)
  {
    return m_pfile->ViewCount();
  }

  unsigned int GERawConverter::getNumEchoes(void)
  {
    return m_processingControl->Value<int>("NumEchoes");
  }
  
  unsigned int GERawConverter::getNumPhases(void)
  {
    return m_processingControl->Value<int>("NumPhases");
  }


  /**
   * Sets the PFile origin to the RDS client
   *
   * TODO: implement!
   */
  void GERawConverter::setRDS(void)
  {
  }
  
  static std::string downloadDataToXML(const GERecon::DownloadDataPointer downloadDataPtr) {
    XMLWriter writer;
    const GERecon::Legacy::LxDownloadDataPointer lxDownloadDataPtr =
      boost::dynamic_pointer_cast<GERecon::Legacy::LxDownloadData>(downloadDataPtr);
    writer.startDocument();
    writer.startElement("Header");
    lxDownloadDataToXML(&writer, lxDownloadDataPtr);
    writer.endDocument();

    return writer.getXML();
  } // function downloadDataToXML()
  

  static void lxDownloadDataToXML(XMLWriter* writer,
                                  const GERecon::Legacy::LxDownloadDataPointer lxDownloadDataPtr)
  {
    const GERecon::Legacy::LxDownloadData& lxDownloadData = *lxDownloadDataPtr.get();
    auto rdbHeader = lxDownloadData.RawHeader();
    const GERecon::Legacy::MrImageDataTypeStruct& imageHeader = lxDownloadData.ImageHeaderData();
    // auto imageHeader = lxDownloadData.ImageHeaderData();
    const boost::shared_ptr<GERecon::Legacy::LxControlSource> controlSource =
      boost::make_shared<GERecon::Legacy::LxControlSource>(lxDownloadDataPtr);
    auto processingControl = controlSource->CreateOrchestraProcessingControl();
    // processingControl->SaveAsXml("processingControl.xml");

    writer->formatElement("NumAcquisitions", "%d", processingControl->Value<int>("NumAcquisitions"));
    writer->formatElement("NumSlices", "%d", processingControl->Value<int>("NumSlices"));
    writer->formatElement("NumPhases", "%d", processingControl->Value<int>("NumPhases"));
    writer->formatElement("NumEchoes", "%d",  processingControl->Value<int>("NumEchoes"));
    writer->formatElement("NumChannels", "%d", processingControl->Value<int>("NumChannels"));
    writer->addBooleanElement("HalfNex", processingControl->ValueStrict<bool>("HalfNex"));
    writer->addBooleanElement("ChopZ", processingControl->ValueStrict<bool>("ChopZ"));
    writer->addBooleanElement("Asset", processingControl->ValueStrict<bool>("Asset"));
                              
    /*
    writer->formatElement("SampleSize", "%u", pfile->SampleSize());
    //writer->formatElement("SampleType", "%d", pfile->SampleType());
    writer->formatElement("BaselineViewCount", "%d", pfile->BaselineViewCount());
    writer->addBooleanElement("IsZEncoded", pfile->IsZEncoded());
    writer->formatElement("PlaneCount", "%d", pfile->PlaneCount());
    writer->formatElement("OutputPhaseCount", "%d", pfile->OutputPhaseCount());
    writer->formatElement("ShotCount", "%d", pfile->ShotCount());
    writer->formatElement("RepetitionCount", "%d", pfile->RepetitionCount());
    writer->addBooleanElement("IsEpi", pfile->IsEpi());
    */
    
    GERecon::Legacy::DicomSeries legacySeries(lxDownloadDataPtr);
    GEDicom::SeriesPointer series = legacySeries.Series();
    GEDicom::SeriesModulePointer seriesModule = series->GeneralModule();
    writer->startElement("Series");
    writer->formatElement("Number", "%d", lxDownloadDataPtr->SeriesNumber());
    writer->formatElement("UID", "%s", seriesModule->UID().c_str());
    writer->formatElement("Description", "%s", seriesModule->SeriesDescription().c_str());
    //writer->formatElement("Modality", "%s", seriesModule->Modality());
    writer->formatElement("Laterality", "%s", seriesModule->Laterality().c_str());
    writer->formatElement("Date", "%s", convert_date(seriesModule->Date()).c_str());
    writer->formatElement("Time", "%s", convert_time(seriesModule->Time()).c_str());
    writer->formatElement("ProtocolName", "%s", seriesModule->ProtocolName().c_str());
    writer->formatElement("OperatorName", "%s", seriesModule->OperatorName().c_str());
    writer->formatElement("PpsDescription", "%s", seriesModule->PpsDescription().c_str());
    //writer->formatElement("PatientEntry", "%s", seriesModule->Entry());
    //writer->formatElement("PatientOrientation", "%s", seriesModule->Orientation());
    writer->endElement();

    GEDicom::StudyPointer study = series->Study();
    GEDicom::StudyModulePointer studyModule = study->GeneralModule();
    writer->startElement("Study");
    writer->formatElement("Number", "%d", studyModule->StudyNumber());
    writer->formatElement("UID", "%s", studyModule->UID().c_str());
    writer->formatElement("Description", "%s", studyModule->StudyDescription().c_str());
    writer->formatElement("Date", "%s", convert_date(studyModule->Date()).c_str());
    writer->formatElement("Time", "%s", convert_time(studyModule->Time()).c_str());
    writer->formatElement("ReferringPhysician", "%s", studyModule->ReferringPhysician().c_str());
    writer->formatElement("AccessionNumber", "%s", studyModule->AccessionNumber().c_str());
    writer->formatElement("ReadingPhysician", "%s", studyModule->ReadingPhysician().c_str());
    writer->endElement();
    
    GEDicom::PatientStudyModulePointer patientStudyModule = study->PatientStudyModule();
    GEDicom::PatientPointer patient = study->Patient();
    GEDicom::PatientModulePointer patientModule = patient->GeneralModule();
    writer->startElement("Patient");
    writer->formatElement("Name", "%s", patientModule->Name().c_str());
    writer->formatElement("ID", "%s", patientModule->ID().c_str());
    writer->formatElement("Birthdate", "%s", convert_date(patientModule->Birthdate()).c_str());
    writer->formatElement("Gender", "%s", patientModule->Gender().c_str());
    writer->formatElement("Age", "%s", patientStudyModule->Age().c_str());
    writer->formatElement("Weight", "%s", patientStudyModule->Weight().c_str());
    writer->formatElement("History", "%s", patientStudyModule->History().c_str());
    writer->endElement();

    GEDicom::EquipmentPointer equipment = series->Equipment();
    GEDicom::EquipmentModulePointer equipmentModule = equipment->GeneralModule();
    writer->startElement("Equipment");
    writer->formatElement("Manufacturer", "%s", equipmentModule->Manufacturer().c_str());
    writer->formatElement("Institution", "%s", equipmentModule->Institution().c_str());
    writer->formatElement("Station", "%s", equipmentModule->Station().c_str());
    writer->formatElement("ManufacturerModel", "%s", equipmentModule->ManufacturerModel().c_str());
    writer->formatElement("DeviceSerialNumber", "%s", equipmentModule->DeviceSerialNumber().c_str());
    writer->formatElement("SoftwareVersion", "%s", equipmentModule->SoftwareVersion().c_str());
    writer->formatElement("PpsPerformedStation", "%s", equipmentModule->PpsPerformedStation().c_str());
    writer->formatElement("PpsPerformedLocation", "%s", equipmentModule->PpsPerformedLocation().c_str());
    writer->endElement();
    
    writer->formatElement("AcquiredXRes", "%d", processingControl->Value<int>("AcquiredXRes"));
    writer->formatElement("AcquiredYRes", "%d", processingControl->Value<int>("AcquiredYRes"));
    writer->formatElement("AcquiredZRes", "%d", processingControl->Value<int>("AcquiredZRes"));
    writer->addBooleanElement("Is3DAcquisition", processingControl->Value<bool>("Is3DAcquisition"));
    writer->formatElement("NumBaselineViews", "%d", processingControl->Value<int>("NumBaselineViews"));
    writer->formatElement("NumVolumes", "%d", processingControl->Value<int>("NumVolumes"));
    writer->addBooleanElement("EvenEchoFrequencyFlip", processingControl->Value<bool>("EvenEchoFrequencyFlip"));
    writer->addBooleanElement("OddEchoFrequencyFlip", processingControl->Value<bool>("OddEchoFrequencyFlip"));
    writer->addBooleanElement("EvenEchoPhaseFlip", processingControl->Value<bool>("EvenEchoPhaseFlip"));
    writer->addBooleanElement("OddEchoPhaseFlip", processingControl->Value<bool>("OddEchoPhaseFlip"));
    writer->addBooleanElement("HalfEcho", processingControl->Value<bool>("HalfEcho"));
    writer->addBooleanElement("HalfNex", processingControl->Value<bool>("HalfNex"));
    writer->addBooleanElement("NoFrequencyWrapData", processingControl->Value<bool>("NoFrequencyWrapData"));
    writer->addBooleanElement("NoPhaseWrapData", processingControl->Value<bool>("NoPhaseWrapData"));
    writer->formatElement("NumAcquisitions", "%d", processingControl->Value<int>("NumAcquisitions"));
    writer->formatElement("NumEchoes", "%d", processingControl->Value<int>("NumEchoes"));
    writer->formatElement("DataSampleSize", "%d", processingControl->Value<int>("DataSampleSize")); // in bytes

    GERecon::PatientPosition patientPosition =
      static_cast<GERecon::PatientPosition>(processingControl->Value<int>("PatientPosition"));
    switch(patientPosition) {
      case GERecon::PatientPosition::Supine:
        writer->formatElement("PatientPosition", "%s", "HFS");
        break;
      case GERecon::PatientPosition::Prone:
        writer->formatElement("PatientPosition", "%s", "HFP");
        break;
      case GERecon::PatientPosition::LeftDescending:
        writer->formatElement("PatientPosition", "%s", "HFDL");
        break;
      case GERecon::PatientPosition::RightDescending:
        writer->formatElement("PatientPosition", "%s", "HFDR");
        break;
      default:
        writer->formatElement("PatientPosition", "%s", "HFS");
        break;
    }
    
    writer->formatElement("PatientEntry", "%d", processingControl->Value<int>("PatientEntry"));
    writer->formatElement("ScanCenter", "%f", processingControl->Value<int>("ScanCenter"));
    writer->formatElement("Landmark", "%f", processingControl->Value<int>("Landmark"));
    writer->formatElement("ExamNumber", "%u", processingControl->Value<int>("ExamNumber"));
    writer->formatElement("CoilConfigUID", "%u", processingControl->Value<int>("CoilConfigUID"));
    writer->formatElement("Coil", "%s", lxDownloadDataPtr->Coil().c_str());
    //writer->formatElement("RawPassSize", "%llu", processingControl->Value<int>("RawPassSize"));
    
    // see AcquisitionParameters documentation for more boolean parameters   
    // ReconstructionParameters
    writer->addBooleanElement("CreateMagnitudeImages", processingControl->Value<bool>("CreateMagnitudeImages"));
    writer->addBooleanElement("CreatePhaseImages", processingControl->Value<bool>("CreatePhaseImages"));
    
    writer->formatElement("TransformXRes", "%d", processingControl->Value<int>("TransformXRes"));
    writer->formatElement("TransformYRes", "%d", processingControl->Value<int>("TransformYRes"));
    writer->formatElement("TransformZRes", "%d", processingControl->Value<int>("TransformZRes"));
    
    writer->addBooleanElement("ChopX", processingControl->Value<bool>("ChopX"));
    writer->addBooleanElement("ChopY", processingControl->Value<bool>("ChopY"));
    writer->addBooleanElement("ChopZ", processingControl->Value<bool>("ChopZ"));
    
    // TODO: map SliceOrder to a string
    // writer->formatElement("SliceOrder", "%s", processingControl->Value<int>("SliceOrder"));
  
    // Image Parameters
    // writer->formatElement("ImageXRes", "%d", processingControl->Value<int>("ImageXRes"));
    // writer->formatElement("ImageYRes", "%d", processingControl->Value<int>("ImageYRes"));
    
    GERecon::PrepData prepData(lxDownloadDataPtr);
    GERecon::ArchiveHeader archiveHeader("ScanArchive", prepData);

    const GERecon::SliceInfoTable sliceTable = processingControl->ValueStrict<GERecon::SliceInfoTable>("SliceTable");
    auto sliceOrientation = sliceTable.SliceOrientation(0);
    auto sliceCorners = sliceTable.AcquiredSliceCorners(0);
    auto imageCorners = GERecon::ImageCorners(sliceCorners, sliceOrientation);
    auto grayscaleImage = GEDicom::GrayscaleImage(128, 128);
    auto dicomImage = GERecon::Legacy::DicomImage(grayscaleImage, 0, imageCorners, series, *lxDownloadDataPtr);
    auto imageModule = dicomImage.ImageModule();
    
    writer->startElement("Image");
    writer->formatElement("PSDName", "%s", imageHeader.psdname);
    writer->formatElement("PSDNameInternal", "%s", imageHeader.psd_iname);
    writer->formatElement("EchoTime", "%s", imageModule->EchoTime().c_str());
    if (processingControl->Value<int>("NumEchoes") > 1)
      writer->formatElement("EchoTime2", "%g", 1e-3 * ((float) rdbHeader.rdb_hdr_te2));
    writer->formatElement("RepetitionTime", "%s", imageModule->RepetitionTime().c_str());
    if (imageModule->InversionTime().length() > 0)
      writer->formatElement("InversionTime", "%s", imageModule->InversionTime().c_str());
    writer->formatElement("ImageType", "%s", imageModule->ImageType().c_str());
    writer->formatElement("ScanSequence", "%s", imageModule->ScanSequence().c_str());
    writer->formatElement("SequenceVariant", "%s", imageModule->SequenceVariant().c_str());
    writer->formatElement("ScanOptions", "%s", imageModule->ScanOptions().c_str());
    writer->formatElement("AcquisitionType", "%d", imageModule->AcqType());
    writer->formatElement("PhaseEncodeDirection", "%d", imageModule->PhaseEncodeDirection());
    writer->formatElement("ImagingFrequency", "%s", imageModule->ImagingFrequency().c_str());
    writer->formatElement("MagneticFieldStrength", "%s", imageModule->MagneticFieldStrength().c_str());
    writer->formatElement("SliceSpacing", "%s", imageModule->SliceSpacing().c_str());
    writer->formatElement("FlipAngle", "%s", imageModule->FlipAngle().c_str());
    writer->formatElement("EchoTrainLength", "%s", imageModule->EchoTrainLength().c_str());
    
    auto imageModuleBase = dicomImage.ImageModuleBase();
    writer->formatElement("AcquisitionDate", "%s", convert_date(imageModuleBase->AcquisitionDate()).c_str());
    writer->formatElement("AcquisitionTime", "%s", convert_time(imageModuleBase->AcquisitionTime()).c_str());
    writer->formatElement("ImageDate", "%s", convert_date(imageModuleBase->ImageDate()).c_str());
    writer->formatElement("ImageTime", "%s", convert_time(imageModuleBase->ImageTime()).c_str());
    
    auto imagePlaneModule = dicomImage.ImagePlaneModule();
    writer->formatElement("ImageOrientation", "%s", imagePlaneModule->ImageOrientation().c_str());
    writer->formatElement("ImagePosition", "%s", imagePlaneModule->ImagePosition().c_str());
    writer->formatElement("SliceThickness", "%f", imagePlaneModule->SliceThickness());
    writer->formatElement("SliceLocation", "%f", imagePlaneModule->SliceLocation());
    writer->formatElement("PixelSizeX", "%f", imagePlaneModule->PixelSizeX());
    writer->formatElement("PixelSizeY", "%f", imagePlaneModule->PixelSizeY());
    
    auto privateAcquisitionModule = dicomImage.PrivateAcquisitionModule();
    writer->formatElement("SecondEcho", "%s", privateAcquisitionModule->SecondEcho().c_str());

    writer->startElement("UserVariable");
    writer->formatElement("User", "%f", imageHeader.user0);
    writer->formatElement("User", "%f", imageHeader.user1);
    writer->formatElement("User", "%f", imageHeader.user2);
    writer->formatElement("User", "%f", imageHeader.user3);
    writer->formatElement("User", "%f", imageHeader.user4);
    writer->formatElement("User", "%f", imageHeader.user5);
    writer->formatElement("User", "%f", imageHeader.user6);
    writer->formatElement("User", "%f", imageHeader.user7);
    writer->formatElement("User", "%f", imageHeader.user8);
    writer->formatElement("User", "%f", imageHeader.user9);
    writer->formatElement("User", "%f", imageHeader.user10);
    writer->formatElement("User", "%f", imageHeader.user11);
    writer->formatElement("User", "%f", imageHeader.user12);
    writer->formatElement("User", "%f", imageHeader.user13);
    writer->formatElement("User", "%f", imageHeader.user14);
    writer->formatElement("User", "%f", imageHeader.user15);
    writer->formatElement("User", "%f", imageHeader.user16);
    writer->formatElement("User", "%f", imageHeader.user17);
    writer->formatElement("User", "%f", imageHeader.user18);
    writer->formatElement("User", "%f", imageHeader.user19);
    writer->formatElement("User", "%f", imageHeader.user20);
    writer->formatElement("User", "%f", imageHeader.user21);
    writer->formatElement("User", "%f", imageHeader.user22);
    writer->formatElement("User", "%f", imageHeader.user23);
    writer->formatElement("User", "%f", imageHeader.user24);
    writer->formatElement("User", "%f", imageHeader.user25);
    writer->formatElement("User", "%f", imageHeader.user26);
    writer->formatElement("User", "%f", imageHeader.user27);
    writer->formatElement("User", "%f", imageHeader.user28);
    writer->formatElement("User", "%f", imageHeader.user29);
    writer->formatElement("User", "%f", imageHeader.user30);
    writer->formatElement("User", "%f", imageHeader.user31);
    writer->formatElement("User", "%f", imageHeader.user32);
    writer->formatElement("User", "%f", imageHeader.user33);
    writer->formatElement("User", "%f", imageHeader.user34);
    writer->formatElement("User", "%f", imageHeader.user35);
    writer->formatElement("User", "%f", imageHeader.user36);
    writer->formatElement("User", "%f", imageHeader.user37);
    writer->formatElement("User", "%f", imageHeader.user38);
    writer->formatElement("User", "%f", imageHeader.user39);
    writer->formatElement("User", "%f", imageHeader.user40);
    writer->formatElement("User", "%f", imageHeader.user41);
    writer->formatElement("User", "%f", imageHeader.user42);
    writer->formatElement("User", "%f", imageHeader.user43);
    writer->formatElement("User", "%f", imageHeader.user44);
    writer->formatElement("User", "%f", imageHeader.user45);
    writer->formatElement("User", "%f", imageHeader.user46);
    writer->formatElement("User", "%f", imageHeader.user47);
    writer->formatElement("User", "%f", imageHeader.user48);
    writer->endElement();

    writer->startElement("ReconUserVariable");
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user0);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user1);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user2);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user3);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user4);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user5);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user6);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user7);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user8);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user9);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user10);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user11);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user12);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user13);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user14);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user15);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user16);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user17);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user18);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user19);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user20);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user21);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user22);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user23);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user24);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user25);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user26);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user27);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user28);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user29);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user30);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user31);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user32);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user33);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user34);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user35);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user36);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user37);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user38);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user39);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user40);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user41);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user42);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user43);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user44);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user45);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user46);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user47);
    writer->formatElement("ReconUser", "%f", rdbHeader.rdb_hdr_user48);
    writer->endElement();
    
    writer->endElement();
  } // function lxDownloadDataToXML()

} // namespace OxToIsmrmrd

