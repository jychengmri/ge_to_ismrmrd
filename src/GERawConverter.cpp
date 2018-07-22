
/** @file GERawConverter.cpp */
#include <iostream>
#include <stdexcept>
#include <string>

// Orchestra
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

// ISMRMRD
#include <ismrmrd/version.h>

// Local
#include "GERawConverter.h"

namespace GeToIsmrmrd {

  std::string convert_date(const std::string& date_str) {
    if (date_str.length() == 8) {
      return date_str.substr(0, 4) + "-"
        + date_str.substr(4, 2) + "-" + date_str.substr(6, 2);
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
   * @param fp raw FILE pointer to GE file
   * @throws std::runtime_error if file cannot be read
   */
  GERawConverter::GERawConverter(const std::string& filepath, bool logging)
    : m_isRDS(false),
      m_anonString(""),
      m_pfile(NULL),
      m_scanArchive(NULL),
      m_downloadDataPtr(NULL),
      m_processingControl(NULL),
      m_log(logging)
  {
    FILE* fp = NULL;
    if (!(fp = fopen(filepath.c_str(), "rb"))) {
      throw std::runtime_error("Failed to open " + filepath);
    }

    m_log << "Reading data from file (" << filepath << ")..." << std::endl;

    if (GERecon::ScanArchive::IsArchiveFilePath(filepath)) {
      m_scanArchive = GERecon::ScanArchive::Create(filepath, GESystem::Archive::LoadMode);

      m_downloadDataPtr = m_scanArchive->LoadDownloadData();
      auto lxDownloadDataPtr =  boost::dynamic_pointer_cast<GERecon::Legacy::LxDownloadData>(m_downloadDataPtr);
      auto controlSource = boost::make_shared<GERecon::Legacy::LxControlSource>(lxDownloadDataPtr);
      m_processingControl = controlSource->CreateOrchestraProcessingControl();

      m_isScanArchive = true;
    }
    else {
      m_pfile = GERecon::Legacy::Pfile::Create(
        filepath,
        GERecon::Legacy::Pfile::AllAvailableAcquisitions,
        GERecon::AnonymizationPolicy(GERecon::AnonymizationPolicy::None));

      m_downloadDataPtr = m_pfile->DownloadData();
      m_processingControl = m_pfile->CreateOrchestraProcessingControl();

      m_isScanArchive = false;
    }

  } // constructor GERawConverter::GERawConverter()


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
    ISMRMRD::IsmrmrdHeader header = lxDownloadDataToIsmrmrdHeader();
    std::stringstream str;
    ISMRMRD::serialize(header, str);
    std::string headerXML (str.str());

    return headerXML;
  }


  /**
   * Specify whether the PFile is an RDS file or not
   */
  void GERawConverter::setRDS(bool isRDS)
  {
    m_isRDS = isRDS;
  }


  /**
   * Specify string to anonymize data
   */
  void GERawConverter::setAnonString(const std::string anonString) {
    m_anonString.assign(anonString);
  }


  ISMRMRD::IsmrmrdHeader GERawConverter::lxDownloadDataToIsmrmrdHeader()
  {
    const GERecon::Legacy::LxDownloadDataPointer lxDownloadDataPtr =
          boost::dynamic_pointer_cast<GERecon::Legacy::LxDownloadData>(m_downloadDataPtr);
    const GERecon::Legacy::LxDownloadData& lxDownloadData = *lxDownloadDataPtr.get();
    auto rdbHeader = lxDownloadData.RawHeader();
    //const GERecon::Legacy::MrImageDataTypeStruct& imageHeader = lxDownloadData.ImageHeaderData();
    auto imageHeader = lxDownloadData.ImageHeaderData();
    const boost::shared_ptr<GERecon::Legacy::LxControlSource> controlSource =
      boost::make_shared<GERecon::Legacy::LxControlSource>(lxDownloadDataPtr);

    bool anonData = !m_anonString.empty();

    GERecon::Legacy::DicomSeries legacySeries(lxDownloadDataPtr);
    GEDicom::SeriesPointer series = legacySeries.Series();
    GEDicom::SeriesModulePointer seriesModule = series->GeneralModule();
    GEDicom::StudyPointer study = series->Study();
    GEDicom::StudyModulePointer studyModule = study->GeneralModule();
    GEDicom::PatientStudyModulePointer patientStudyModule = study->PatientStudyModule();
    GEDicom::PatientPointer patient = study->Patient();
    GEDicom::PatientModulePointer patientModule = patient->GeneralModule();
    const GERecon::SliceInfoTable sliceTable = m_processingControl->ValueStrict<GERecon::SliceInfoTable>("SliceTable");
    auto sliceOrientation = sliceTable.SliceOrientation(0);
    auto sliceCorners = sliceTable.AcquiredSliceCorners(0);
    auto imageCorners = GERecon::ImageCorners(sliceCorners, sliceOrientation);
    auto grayscaleImage = GEDicom::GrayscaleImage(128, 128);
    auto dicomImage = GERecon::Legacy::DicomImage(grayscaleImage, 0, imageCorners, series, *lxDownloadDataPtr);
    auto imageModule = dicomImage.ImageModule();
    auto imagePlaneModule = dicomImage.ImagePlaneModule();

    m_log << "Building ISMRMRD header..." << std::endl;
    ISMRMRD::IsmrmrdHeader ismrmrd_header;
    ismrmrd_header.version = ISMRMRD_XMLHDR_VERSION;

    if (anonData) {
      m_log << "  Anonymizing dataset (" << m_anonString << ")..." << std::endl;
    }

    m_log << "  Loading subject information..." << std::endl;
    ISMRMRD::SubjectInformation subjectInformation;
    if (anonData) {
      subjectInformation.patientName = m_anonString;
      subjectInformation.patientID = m_anonString;
    }
    else {
      subjectInformation.patientName = patientModule->Name().c_str();
      subjectInformation.patientWeight_kg = std::stof(patientStudyModule->Weight());
      subjectInformation.patientID = patientModule->ID().c_str();
      if (!patientModule->Birthdate().empty())
        subjectInformation.patientBirthdate = convert_date(patientModule->Birthdate()).c_str();
    }
    if (!patientModule->Gender().empty())
      subjectInformation.patientGender = patientModule->Gender().c_str();
    ismrmrd_header.subjectInformation = subjectInformation;

    m_log << "  Loading study information..." << std::endl;
    ISMRMRD::StudyInformation studyInformation;
    if (anonData) {
      studyInformation.studyID = m_anonString;
      studyInformation.studyDescription = m_anonString;
      studyInformation.studyInstanceUID = m_anonString;
    }
    else {
      if (!studyModule->Date().empty())
        studyInformation.studyDate = convert_date(studyModule->Date()).c_str();
      studyInformation.studyTime = convert_time(studyModule->Time()).c_str();
      studyInformation.studyID = std::to_string(studyModule->StudyNumber());
      studyInformation.accessionNumber = std::strtol(studyModule->AccessionNumber().c_str(), NULL, 0);
      studyInformation.referringPhysicianName = studyModule->ReferringPhysician().c_str();
      studyInformation.studyDescription = studyModule->StudyDescription().c_str();
      studyInformation.studyInstanceUID = studyModule->UID().c_str();
    }
    ismrmrd_header.studyInformation = studyInformation;
    //writer->formatElement("ReadingPhysician", "%s", studyModule->ReadingPhysician().c_str());

    m_log << "  Loading measurement information..." << std::endl;
    ISMRMRD::MeasurementInformation measurementInformation;
    if (anonData) {
      measurementInformation.protocolName = m_anonString;
      measurementInformation.seriesDescription = m_anonString;
      measurementInformation.seriesInstanceUIDRoot = m_anonString;
    } else {
      // measurementInformation.measurementID = lxDownloadDataPtr->SeriesNumber();
      if (!seriesModule->Date().empty())
        measurementInformation.seriesDate = convert_date(seriesModule->Date()).c_str();
      measurementInformation.seriesTime = convert_time(seriesModule->Time()).c_str();
      measurementInformation.protocolName = seriesModule->ProtocolName().c_str();
      measurementInformation.seriesDescription = seriesModule->SeriesDescription().c_str();
      //measurementInformation.measurementDependency = ?
      measurementInformation.seriesInstanceUIDRoot = seriesModule->UID().c_str();
      // measurementInformation.frameOfReferenceUID = ?
      // measurementInformation.referencedImageSequence = ?
      // writer->formatElement("Laterality", "%s", seriesModule->Laterality().c_str());
      // writer->formatElement("OperatorName", "%s", seriesModule->OperatorName().c_str());
    }
    measurementInformation.initialSeriesNumber = lxDownloadDataPtr->SeriesNumber();
    GERecon::PatientPosition patientPosition = static_cast<GERecon::PatientPosition>(
      m_processingControl->Value<int>("PatientPosition"));
    switch(patientPosition)
    {
    case GERecon::PatientPosition::Supine:
      measurementInformation.patientPosition = "HFS";
      break;
    case GERecon::PatientPosition::Prone:
      measurementInformation.patientPosition = "HFP";
      break;
    case GERecon::PatientPosition::LeftDescending:
      measurementInformation.patientPosition = "HFDL";
      break;
    case GERecon::PatientPosition::RightDescending:
      measurementInformation.patientPosition = "HFDR";
      break;
    default:
      measurementInformation.patientPosition = "HFS";
      break;
    }
    ismrmrd_header.measurementInformation = measurementInformation;

    m_log << "  Loading acquisition system information..." << std::endl;
    ISMRMRD::AcquisitionSystemInformation acquisitionSystemInformation;
    GEDicom::EquipmentPointer equipment = series->Equipment();
    GEDicom::EquipmentModulePointer equipmentModule = equipment->GeneralModule();
    acquisitionSystemInformation.systemVendor = equipmentModule->Manufacturer().c_str();
    acquisitionSystemInformation.systemModel = equipmentModule->ManufacturerModel().c_str();
    acquisitionSystemInformation.systemFieldStrength_T = std::strtof(imageModule->MagneticFieldStrength().c_str(), 0);
    acquisitionSystemInformation.relativeReceiverNoiseBandwidth = rdbHeader.rdb_hdr_bw;
    acquisitionSystemInformation.receiverChannels = m_processingControl->Value<int>("NumChannels");
    ISMRMRD::CoilLabel coilLabel;
    // TODO: Will have problems converting int to short
    coilLabel.coilNumber = (unsigned short)m_processingControl->Value<int>("CoilConfigUID");
    coilLabel.coilName = lxDownloadDataPtr->Coil().c_str();
    acquisitionSystemInformation.coilLabel.push_back(coilLabel);
    acquisitionSystemInformation.institutionName = equipmentModule->Institution().c_str();
    acquisitionSystemInformation.stationName = equipmentModule->Station().c_str();
    //writer->formatElement("DeviceSerialNumber", "%s", equipmentModule->DeviceSerialNumber().c_str());
    //writer->formatElement("SoftwareVersion", "%s", equipmentModule->SoftwareVersion().c_str());
    //writer->formatElement("PpsPerformedStation", "%s", equipmentModule->PpsPerformedStation().c_str());
    //writer->formatElement("PpsPerformedLocation", "%s", equipmentModule->PpsPerformedLocation().c_str());
    ismrmrd_header.acquisitionSystemInformation = acquisitionSystemInformation;

    m_log << "  Loading experimental conditions..." << std::endl;
    ismrmrd_header.experimentalConditions.H1resonanceFrequency_Hz = std::strtol(imageModule->ImagingFrequency().c_str(), NULL, 0);

    m_log << "  Loading encoding information..." << std::endl;
    ISMRMRD::Encoding encoding;
    bool is3D = m_processingControl->Value<bool>("Is3DAcquisition");
    int acquiredXRes = m_processingControl->Value<int>("AcquiredXRes");
    int acquiredYRes = m_processingControl->Value<int>("AcquiredYRes");
    int acquiredZRes = m_processingControl->Value<int>("AcquiredZRes");
    int transformXRes = m_processingControl->Value<int>("TransformXRes");
    int transformYRes = m_processingControl->Value<int>("TransformYRes");
    int transformZRes = m_processingControl->Value<int>("AcquiredZRes");
    float pixelSizeX = imagePlaneModule->PixelSizeX();
    float pixelSizeY = imagePlaneModule->PixelSizeY();
    float pixelSizeZ = imagePlaneModule->SliceThickness();
    short zipFactor = rdbHeader.rdb_hdr_zip_factor;
    encoding.encodedSpace.matrixSize.x = acquiredXRes;
    encoding.encodedSpace.matrixSize.y = acquiredYRes;
    encoding.encodedSpace.matrixSize.z = acquiredZRes;
    encoding.encodedSpace.fieldOfView_mm.x = transformXRes * pixelSizeX;
    encoding.encodedSpace.fieldOfView_mm.y = transformYRes * pixelSizeY;
    if (is3D)
      encoding.encodedSpace.fieldOfView_mm.z = acquiredZRes * pixelSizeZ;
    else
      encoding.encodedSpace.fieldOfView_mm.z = pixelSizeZ;
    encoding.reconSpace.matrixSize.x = transformXRes;
    encoding.reconSpace.matrixSize.y = transformYRes;
    encoding.reconSpace.matrixSize.z = transformZRes * zipFactor;
    encoding.reconSpace.fieldOfView_mm.x = transformXRes * pixelSizeX;
    encoding.reconSpace.fieldOfView_mm.y = transformYRes * pixelSizeY;
    if (is3D)
      encoding.reconSpace.fieldOfView_mm.z = acquiredZRes * pixelSizeZ;
    else
      encoding.reconSpace.fieldOfView_mm.z = pixelSizeZ;
    encoding.trajectory = "cartesian"; //ISMRMRD::TrajectoryType::CARTESIAN;
    encoding.encodingLimits.kspace_encoding_step_1 = ISMRMRD::Limit(0, acquiredYRes - 1, acquiredYRes / 2);
    if (is3D) {
      encoding.encodingLimits.kspace_encoding_step_2 = ISMRMRD::Limit(0, acquiredZRes - 1, acquiredZRes / 2);
      encoding.encodingLimits.slice = ISMRMRD::Limit(0, 0, 0);
    }
    else {
      encoding.encodingLimits.kspace_encoding_step_2 = ISMRMRD::Limit(0, 0, 0);
      encoding.encodingLimits.slice = ISMRMRD::Limit(0, acquiredZRes - 1, acquiredZRes / 2);
    }
    unsigned short numEchoes = (unsigned short) m_processingControl->Value<int>("NumEchoes");
    encoding.encodingLimits.contrast = ISMRMRD::Limit(0, numEchoes - 1, numEchoes / 2);
    unsigned short numPhases = (unsigned short) m_processingControl->Value<int>("NumPhases");
    encoding.encodingLimits.phase = ISMRMRD::Limit(0, numPhases - 1, numPhases / 2);
    unsigned short echoTrainLength = (unsigned short) imageHeader.echo_trn_len;
    encoding.encodingLimits.segment = ISMRMRD::Limit(0, echoTrainLength - 1, echoTrainLength / 2);
    // encoding.parallelImaging
    ismrmrd_header.encoding.push_back(encoding);

    ISMRMRD::SequenceParameters sequenceParameters;
    std::vector<float> TR;
    TR.push_back(std::strtof(imageModule->RepetitionTime().c_str(), 0));
    sequenceParameters.TR = TR;
    std::vector<float> TE;
    TE.push_back(1e-3 * rdbHeader.rdb_hdr_te);
    if (numEchoes > 1)
      TE.push_back(1e-3 * rdbHeader.rdb_hdr_te2);
    sequenceParameters.TE = TE;
    if (imageModule->InversionTime().length() > 0) {
      std::vector<float> TI;
      TI.push_back(std::strtof(imageModule->InversionTime().c_str(), 0));
      sequenceParameters.TI = TI;
    }
    std::vector<float> flipAngle_deg;
    flipAngle_deg.push_back(std::strtof(imageModule->FlipAngle().c_str(), 0));
    sequenceParameters.flipAngle_deg = flipAngle_deg;
    sequenceParameters.sequence_type = imageModule->ScanSequence().c_str();
    ismrmrd_header.sequenceParameters = sequenceParameters;

    ISMRMRD::UserParameters userParameters;
    userParameters.userParameterString.push_back({"GitCommitHash", GIT_COMMIT_HASH});
    userParameters.userParameterString.push_back({"GitBranch", GIT_BRANCH});
    if (m_isScanArchive)
      userParameters.userParameterString.push_back({"OrigFileFormat", "ScanArchive"});
    else
      userParameters.userParameterString.push_back({"OrigFileFormat", "PFile"});
    userParameters.userParameterString.push_back({"PSDName", imageHeader.psdname});
    userParameters.userParameterString.push_back({"PSDNameInternal", imageHeader.psd_iname});
    userParameters.userParameterString.push_back({"History", patientStudyModule->History().c_str()});

    userParameters.userParameterLong.push_back({.name = "ChopX", .value = m_processingControl->Value<bool>("ChopX")});
    userParameters.userParameterLong.push_back({.name = "ChopY", .value = m_processingControl->Value<bool>("ChopY")});
    userParameters.userParameterLong.push_back({.name = "ChopZ", .value = m_processingControl->Value<bool>("ChopZ")});
    userParameters.userParameterLong.push_back({.name = "RHRecon", .value = rdbHeader.rdb_hdr_recon});
    userParameters.userParameterLong.push_back({.name = "KAcqUID", .value = rdbHeader.rdb_hdr_kacq_uid});
    userParameters.userParameterLong.push_back({"CoilConfigUID", m_processingControl->Value<int>("CoilConfigUID")});

    userParameters.userParameterDouble.push_back({.name = "ReconUser0", .value = rdbHeader.rdb_hdr_user0});
    userParameters.userParameterDouble.push_back({.name = "ReconUser1", .value = rdbHeader.rdb_hdr_user1});
    userParameters.userParameterDouble.push_back({.name = "ReconUser2", .value = rdbHeader.rdb_hdr_user2});
    userParameters.userParameterDouble.push_back({.name = "ReconUser3", .value = rdbHeader.rdb_hdr_user3});
    userParameters.userParameterDouble.push_back({.name = "ReconUser4", .value = rdbHeader.rdb_hdr_user4});
    userParameters.userParameterDouble.push_back({.name = "ReconUser5", .value = rdbHeader.rdb_hdr_user5});
    userParameters.userParameterDouble.push_back({.name = "ReconUser6", .value = rdbHeader.rdb_hdr_user6});
    userParameters.userParameterDouble.push_back({.name = "ReconUser7", .value = rdbHeader.rdb_hdr_user7});
    userParameters.userParameterDouble.push_back({.name = "ReconUser8", .value = rdbHeader.rdb_hdr_user8});
    userParameters.userParameterDouble.push_back({.name = "ReconUser9", .value = rdbHeader.rdb_hdr_user9});
    userParameters.userParameterDouble.push_back({.name = "ReconUser10", .value = rdbHeader.rdb_hdr_user10});
    userParameters.userParameterDouble.push_back({.name = "ReconUser11", .value = rdbHeader.rdb_hdr_user11});
    userParameters.userParameterDouble.push_back({.name = "ReconUser12", .value = rdbHeader.rdb_hdr_user12});
    userParameters.userParameterDouble.push_back({.name = "ReconUser13", .value = rdbHeader.rdb_hdr_user13});
    userParameters.userParameterDouble.push_back({.name = "ReconUser14", .value = rdbHeader.rdb_hdr_user14});
    userParameters.userParameterDouble.push_back({.name = "ReconUser15", .value = rdbHeader.rdb_hdr_user15});
    userParameters.userParameterDouble.push_back({.name = "ReconUser16", .value = rdbHeader.rdb_hdr_user16});
    userParameters.userParameterDouble.push_back({.name = "ReconUser17", .value = rdbHeader.rdb_hdr_user17});
    userParameters.userParameterDouble.push_back({.name = "ReconUser18", .value = rdbHeader.rdb_hdr_user18});
    userParameters.userParameterDouble.push_back({.name = "ReconUser19", .value = rdbHeader.rdb_hdr_user19});
    userParameters.userParameterDouble.push_back({.name = "ReconUser20", .value = rdbHeader.rdb_hdr_user20});
    userParameters.userParameterDouble.push_back({.name = "ReconUser21", .value = rdbHeader.rdb_hdr_user21});
    userParameters.userParameterDouble.push_back({.name = "ReconUser22", .value = rdbHeader.rdb_hdr_user22});
    userParameters.userParameterDouble.push_back({.name = "ReconUser23", .value = rdbHeader.rdb_hdr_user23});
    userParameters.userParameterDouble.push_back({.name = "ReconUser24", .value = rdbHeader.rdb_hdr_user24});
    userParameters.userParameterDouble.push_back({.name = "ReconUser25", .value = rdbHeader.rdb_hdr_user25});
    userParameters.userParameterDouble.push_back({.name = "ReconUser26", .value = rdbHeader.rdb_hdr_user26});
    userParameters.userParameterDouble.push_back({.name = "ReconUser27", .value = rdbHeader.rdb_hdr_user27});
    userParameters.userParameterDouble.push_back({.name = "ReconUser28", .value = rdbHeader.rdb_hdr_user28});
    userParameters.userParameterDouble.push_back({.name = "ReconUser29", .value = rdbHeader.rdb_hdr_user29});
    userParameters.userParameterDouble.push_back({.name = "ReconUser30", .value = rdbHeader.rdb_hdr_user30});
    userParameters.userParameterDouble.push_back({.name = "ReconUser31", .value = rdbHeader.rdb_hdr_user31});
    userParameters.userParameterDouble.push_back({.name = "ReconUser32", .value = rdbHeader.rdb_hdr_user32});
    userParameters.userParameterDouble.push_back({.name = "ReconUser33", .value = rdbHeader.rdb_hdr_user33});
    userParameters.userParameterDouble.push_back({.name = "ReconUser34", .value = rdbHeader.rdb_hdr_user34});
    userParameters.userParameterDouble.push_back({.name = "ReconUser35", .value = rdbHeader.rdb_hdr_user35});
    userParameters.userParameterDouble.push_back({.name = "ReconUser36", .value = rdbHeader.rdb_hdr_user36});
    userParameters.userParameterDouble.push_back({.name = "ReconUser37", .value = rdbHeader.rdb_hdr_user37});
    userParameters.userParameterDouble.push_back({.name = "ReconUser38", .value = rdbHeader.rdb_hdr_user38});
    userParameters.userParameterDouble.push_back({.name = "ReconUser39", .value = rdbHeader.rdb_hdr_user39});
    userParameters.userParameterDouble.push_back({.name = "ReconUser40", .value = rdbHeader.rdb_hdr_user40});
    userParameters.userParameterDouble.push_back({.name = "ReconUser41", .value = rdbHeader.rdb_hdr_user41});
    userParameters.userParameterDouble.push_back({.name = "ReconUser42", .value = rdbHeader.rdb_hdr_user42});
    userParameters.userParameterDouble.push_back({.name = "ReconUser43", .value = rdbHeader.rdb_hdr_user43});
    userParameters.userParameterDouble.push_back({.name = "ReconUser44", .value = rdbHeader.rdb_hdr_user44});
    userParameters.userParameterDouble.push_back({.name = "ReconUser45", .value = rdbHeader.rdb_hdr_user45});
    userParameters.userParameterDouble.push_back({.name = "ReconUser46", .value = rdbHeader.rdb_hdr_user46});
    userParameters.userParameterDouble.push_back({.name = "ReconUser47", .value = rdbHeader.rdb_hdr_user47});
    userParameters.userParameterDouble.push_back({.name = "ReconUser48", .value = rdbHeader.rdb_hdr_user48});

    userParameters.userParameterDouble.push_back({.name = "User0", .value = imageHeader.user0});
    userParameters.userParameterDouble.push_back({.name = "User1", .value = imageHeader.user1});
    userParameters.userParameterDouble.push_back({.name = "User2", .value = imageHeader.user2});
    userParameters.userParameterDouble.push_back({.name = "User3", .value = imageHeader.user3});
    userParameters.userParameterDouble.push_back({.name = "User4", .value = imageHeader.user4});
    userParameters.userParameterDouble.push_back({.name = "User5", .value = imageHeader.user5});
    userParameters.userParameterDouble.push_back({.name = "User6", .value = imageHeader.user6});
    userParameters.userParameterDouble.push_back({.name = "User7", .value = imageHeader.user7});
    userParameters.userParameterDouble.push_back({.name = "User8", .value = imageHeader.user8});
    userParameters.userParameterDouble.push_back({.name = "User9", .value = imageHeader.user9});
    userParameters.userParameterDouble.push_back({.name = "User10", .value = imageHeader.user10});
    userParameters.userParameterDouble.push_back({.name = "User11", .value = imageHeader.user11});
    userParameters.userParameterDouble.push_back({.name = "User12", .value = imageHeader.user12});
    userParameters.userParameterDouble.push_back({.name = "User13", .value = imageHeader.user13});
    userParameters.userParameterDouble.push_back({.name = "User14", .value = imageHeader.user14});
    userParameters.userParameterDouble.push_back({.name = "User15", .value = imageHeader.user15});
    userParameters.userParameterDouble.push_back({.name = "User16", .value = imageHeader.user16});
    userParameters.userParameterDouble.push_back({.name = "User17", .value = imageHeader.user17});
    userParameters.userParameterDouble.push_back({.name = "User18", .value = imageHeader.user18});
    userParameters.userParameterDouble.push_back({.name = "User19", .value = imageHeader.user19});
    userParameters.userParameterDouble.push_back({.name = "User20", .value = imageHeader.user20});
    userParameters.userParameterDouble.push_back({.name = "User21", .value = imageHeader.user21});
    userParameters.userParameterDouble.push_back({.name = "User22", .value = imageHeader.user22});
    userParameters.userParameterDouble.push_back({.name = "User23", .value = imageHeader.user23});
    userParameters.userParameterDouble.push_back({.name = "User24", .value = imageHeader.user24});
    userParameters.userParameterDouble.push_back({.name = "User25", .value = imageHeader.user25});
    userParameters.userParameterDouble.push_back({.name = "User26", .value = imageHeader.user26});
    userParameters.userParameterDouble.push_back({.name = "User27", .value = imageHeader.user27});
    userParameters.userParameterDouble.push_back({.name = "User28", .value = imageHeader.user28});
    userParameters.userParameterDouble.push_back({.name = "User29", .value = imageHeader.user29});
    userParameters.userParameterDouble.push_back({.name = "User30", .value = imageHeader.user30});
    userParameters.userParameterDouble.push_back({.name = "User31", .value = imageHeader.user31});
    userParameters.userParameterDouble.push_back({.name = "User32", .value = imageHeader.user32});
    userParameters.userParameterDouble.push_back({.name = "User33", .value = imageHeader.user33});
    userParameters.userParameterDouble.push_back({.name = "User34", .value = imageHeader.user34});
    userParameters.userParameterDouble.push_back({.name = "User35", .value = imageHeader.user35});
    userParameters.userParameterDouble.push_back({.name = "User36", .value = imageHeader.user36});
    userParameters.userParameterDouble.push_back({.name = "User37", .value = imageHeader.user37});
    userParameters.userParameterDouble.push_back({.name = "User38", .value = imageHeader.user38});
    userParameters.userParameterDouble.push_back({.name = "User39", .value = imageHeader.user39});
    userParameters.userParameterDouble.push_back({.name = "User40", .value = imageHeader.user40});
    userParameters.userParameterDouble.push_back({.name = "User41", .value = imageHeader.user41});
    userParameters.userParameterDouble.push_back({.name = "User42", .value = imageHeader.user42});
    userParameters.userParameterDouble.push_back({.name = "User43", .value = imageHeader.user43});
    userParameters.userParameterDouble.push_back({.name = "User44", .value = imageHeader.user44});
    userParameters.userParameterDouble.push_back({.name = "User45", .value = imageHeader.user45});
    userParameters.userParameterDouble.push_back({.name = "User46", .value = imageHeader.user46});
    userParameters.userParameterDouble.push_back({.name = "User47", .value = imageHeader.user47});
    userParameters.userParameterDouble.push_back({.name = "User48", .value = imageHeader.user48});

    ismrmrd_header.userParameters = userParameters;

    /*
    writer->formatElement("ImageType", "%s", imageModule->ImageType().c_str());
    writer->formatElement("ScanSequence", "%s", imageModule->ScanSequence().c_str());
    writer->formatElement("SequenceVariant", "%s", imageModule->SequenceVariant().c_str());
    writer->formatElement("ScanOptions", "%s", imageModule->ScanOptions().c_str());
    writer->formatElement("AcquisitionType", "%d", imageModule->AcqType());
    writer->formatElement("PhaseEncodeDirection", "%d", imageModule->PhaseEncodeDirection());
    writer->formatElement("SliceSpacing", "%s", imageModule->SliceSpacing().c_str());
    writer->formatElement("EchoTrainLength", "%s", imageModule->EchoTrainLength().c_str());
     */

    /*
    Optional<UserParameters> userParameters;
    */

    /*
    writer->formatElement("NumAcquisitions", "%d", processingControl->Value<int>("NumAcquisitions"));
    writer->formatElement("NumSlices", "%d", processingControl->Value<int>("NumSlices"));
    writer->addBooleanElement("HalfNex", processingControl->ValueStrict<bool>("HalfNex"));
    writer->addBooleanElement("ChopZ", processingControl->ValueStrict<bool>("ChopZ"));
    writer->addBooleanElement("Asset", processingControl->ValueStrict<bool>("Asset"));


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
    //writer->formatElement("RawPassSize", "%llu", processingControl->Value<int>("RawPassSize"));

    // see AcquisitionParameters documentation for more boolean parameters
    // ReconstructionParameters
    writer->addBooleanElement("CreateMagnitudeImages", processingControl->Value<bool>("CreateMagnitudeImages"));
    writer->addBooleanElement("CreatePhaseImages", processingControl->Value<bool>("CreatePhaseImages"));


    // TODO: map SliceOrder to a string
    // writer->formatElement("SliceOrder", "%s", processingControl->Value<int>("SliceOrder"));

    // Image Parameters
    // writer->formatElement("ImageXRes", "%d", processingControl->Value<int>("ImageXRes"));
    // writer->formatElement("ImageYRes", "%d", processingControl->Value<int>("ImageYRes"));


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
    */

    return ismrmrd_header;
  } // function lxDownloadDataToXML()


  size_t GERawConverter::appendAcquisitions(ISMRMRD::Dataset& d)
  {
    size_t numData = 0 ; //appendNoiseInformation(d);
    if (m_isScanArchive)
      return numData + appendAcquisitionsFromArchive(d);
    else {
      if (m_isRDS)
        return numData + appendAcquisitionsFromPfile(d);
      else
        return numData + appendImagesFromPfile(d);
    }
  } // function GERawConverter::appendAcquisitions()


  size_t GERawConverter::appendNoiseInformation(ISMRMRD::Dataset &d)
  {
    auto lxDownloadDataPtr =  boost::dynamic_pointer_cast<GERecon::Legacy::LxDownloadData>(m_downloadDataPtr);
    const GERecon::Legacy::LxDownloadData& lxDownloadData = *lxDownloadDataPtr.get();
    const GERecon::Legacy::PrescanHeaderStruct& prescanHeader = lxDownloadData.PrescanHeader();
    unsigned int numChannels = (unsigned int) m_processingControl->Value<int>("NumChannels");

    m_log << "Loading noise std/mean values..." << std::endl;
    std::vector<size_t> dims = {numChannels};
    ISMRMRD::NDArray<float> recStd(dims);
    ISMRMRD::NDArray<float> recMean(dims);
    for (unsigned int i_channel = 0; i_channel < numChannels; i_channel++) {
      recStd(i_channel) = prescanHeader.rec_std[i_channel];
      recMean(i_channel) = prescanHeader.rec_mean[i_channel];
    }

    d.appendNDArray("rec_std", recStd);
    d.appendNDArray("rec_mean", recMean);

    return 2;
  }


  size_t GERawConverter::appendImagesFromPfile(ISMRMRD::Dataset& d)
  {
    if (m_isScanArchive)
      return 0;

    //const GERecon::Control::ProcessingControlPointer processingControl(m_pfile->CreateOrchestraProcessingControl());
    //auto lxDownloadDataPtr =  boost::dynamic_pointer_cast<GERecon::Legacy::LxDownloadData>(m_downloadDataPtr);

    unsigned int lenFrame = (unsigned int) m_processingControl->Value<int>("AcquiredXRes");
    unsigned int numViews = (unsigned int) m_processingControl->Value<int>("AcquiredYRes");
    unsigned int numSlices = (unsigned int) m_processingControl->Value<int>("AcquiredZRes");
    unsigned int numChannels = (unsigned int) m_processingControl->Value<int>("NumChannels");
    unsigned int numEchoes = (unsigned int) m_processingControl->Value<int>("NumEchoes");
    unsigned int numPhases = (unsigned int) m_processingControl->Value<int>("NumPhases");

    size_t numVolumes = 0;

    for (unsigned int i_phase = 0; i_phase < numPhases; i_phase++) {
      for (unsigned int i_echo = 0; i_echo < numEchoes; i_echo++) {
        ISMRMRD::Image<std::complex<float> > kspace(lenFrame, numViews, numSlices, numChannels);
        kspace.setImageType(ISMRMRD::ISMRMRD_ImageTypes::ISMRMRD_IMTYPE_COMPLEX);
        kspace.setContrast(i_echo);
        kspace.setPhase(i_phase);
        // ISMRMRD::ImageHeader header = kspace.getHead();

        // Pfile is stored as (readout, views, echoes, slice, channel)
        m_log << "Reading volume (Echo: " << i_echo << ", Phase: " << i_phase << ")..." << std::endl;
#pragma omp parallel for collapse(2)
        for (unsigned int i_channel = 0; i_channel < numChannels; i_channel++) {
          for (unsigned int i_slice = 0; i_slice < numSlices; i_slice++) {
            MDArray::ComplexFloatMatrix kspaceFromFile;
            if (m_pfile->IsZEncoded()) {
              auto kSpaceRead = m_pfile->KSpaceData<float>(
                GERecon::Legacy::Pfile::PassSlicePair(i_phase, i_slice), i_echo, i_channel);
              kspaceFromFile.reference(kSpaceRead);
            }
            else {
              auto kSpaceRead = m_pfile->KSpaceData<float>(i_slice, i_echo, i_channel, i_phase);
              kspaceFromFile.reference(kSpaceRead);
            }

            for (unsigned int i_view = 0; i_view < numViews; i_view++) {
              for (unsigned int i = 0 ; i < lenFrame ; i++)
                kspace(i, i_view, i_slice, i_channel) = kspaceFromFile((int)i, (int)i_view);
            } // for (i_view)
          } // for (i_slice)
        } // for (i_channel)
        d.appendImage("kspace", kspace);
        numVolumes++;
      } // for (i_echo)
    } // for (i_phase)

    return numVolumes;
  } // function GERawConverter::appendImagesFromPfile()


  size_t GERawConverter::appendAcquisitionsFromPfile(ISMRMRD::Dataset& d)
  {
    if (m_isScanArchive)
      return 0;

    const GERecon::Control::ProcessingControlPointer processingControl(m_pfile->CreateOrchestraProcessingControl());
    auto lxDownloadDataPtr =  boost::dynamic_pointer_cast<GERecon::Legacy::LxDownloadData>(m_downloadDataPtr);
    const GERecon::Legacy::LxDownloadData& lxDownloadData = *lxDownloadDataPtr.get();
    auto rdbHeader = lxDownloadData.RawHeader();

    float bandwidth = rdbHeader.rdb_hdr_bw;
    float sample_time_us = 1.0 / (bandwidth * 1e-3);

    unsigned int lenFrame = (unsigned int) m_processingControl->Value<int>("AcquiredXRes");
    unsigned int numChannels = (unsigned int) m_processingControl->Value<int>("NumChannels");

    size_t numViews = m_pfile->ViewCount();
    m_log << "Number of views: " << numViews << std::endl;

    for (size_t i_view = 0; i_view < numViews; i_view++) {
      ISMRMRD::Acquisition ismrmrd_acq;
      ismrmrd_acq.resize(lenFrame, numChannels);
      ismrmrd_acq.scan_counter() = i_view;
      ismrmrd_acq.discard_pre() = 0;
      ismrmrd_acq.discard_post() = 0;
      ismrmrd_acq.sample_time_us() = sample_time_us;

#pragma omp parallel for
      for (size_t i_channel = 0; i_channel < numChannels; i_channel++) {
        MDArray::ComplexFloatVector kspaceFromFile = m_pfile->ViewData<float>(i_view, i_channel);
        for (unsigned int i_readout = 0; i_readout < lenFrame; i_readout++) {
          ismrmrd_acq.data(i_readout, i_channel) = kspaceFromFile(i_readout);
        }
      }
      d.appendAcquisition(ismrmrd_acq);
    }

    return numViews;
  } // function GERawConverter::appendAcquisitionsFromPfile()

  size_t GERawConverter::appendAcquisitionsFromArchive(ISMRMRD::Dataset& d) {
    if (!m_isScanArchive)
      return 0;

    GERecon::Acquisition::ArchiveStoragePointer archiveStorage =
      GERecon::Acquisition::ArchiveStorage::Create(m_scanArchive);
    const GERecon::Legacy::LxDownloadDataPointer lxDownloadDataPtr =
      boost::dynamic_pointer_cast<GERecon::Legacy::LxDownloadData>(m_downloadDataPtr);
    const GERecon::Legacy::LxDownloadData& lxDownloadData = *lxDownloadDataPtr.get();
    auto rdbHeader = lxDownloadData.RawHeader();

    const size_t numControls = archiveStorage->AvailableControlCount();
    int lenReadout = m_processingControl->Value<int>("AcquiredXRes");
    int numChannels = m_processingControl->Value<int>("NumChannels");
    bool is3D = m_processingControl->Value<bool>("Is3DAcquisition");
    float bandwidth = rdbHeader.rdb_hdr_bw;
    float sample_time_us = 1.0 / (bandwidth * 1e-3);

    //m_log << "Bandwidth" << bandwidth << std::endl;
    size_t i_acquisition = 0;

    m_log << "Num controls: " << numControls << std::endl;

    for(size_t i_control = 0; i_control < numControls; i_control++) {
      const GERecon::Acquisition::FrameControlPointer controlPacketAndFrameData = archiveStorage->NextFrameControl();
      if(controlPacketAndFrameData->Control().Opcode() == GERecon::Acquisition::ProgrammableOpcode) {

        const GERecon::Acquisition::ProgrammableControlPacket framePacket =
          controlPacketAndFrameData->Control().Packet().As<GERecon::Acquisition::ProgrammableControlPacket>();
        int viewValue = GERecon::Acquisition::GetPacketValue(framePacket.viewNumH, framePacket.viewNumL);
        if (viewValue != 0) {
          ISMRMRD::Acquisition ismrmrd_acq;
          ismrmrd_acq.resize(lenReadout, numChannels);
          ismrmrd_acq.idx().contrast = framePacket.echoNum;
          ismrmrd_acq.idx().kspace_encode_step_1 = viewValue - 1;
          if (is3D) {
            ismrmrd_acq.idx().kspace_encode_step_2 = GERecon::Acquisition::GetPacketValue(
              framePacket.sliceNumH, framePacket.sliceNumL);
            ismrmrd_acq.idx().slice = 0;
          }
          else {
            ismrmrd_acq.idx().kspace_encode_step_2 = 0;
            ismrmrd_acq.idx().slice = GERecon::Acquisition::GetPacketValue(
              framePacket.sliceNumH, framePacket.sliceNumL);
          }
          ismrmrd_acq.idx().segment = GERecon::Acquisition::GetPacketValue(
            framePacket.echoTrainIndexH, framePacket.echoTrainIndexL);
          ismrmrd_acq.scan_counter() = i_acquisition++;
          ismrmrd_acq.discard_pre() = 0;
          ismrmrd_acq.discard_post() = 0;
          ismrmrd_acq.sample_time_us() = sample_time_us;
          ismrmrd_acq.user_int()[0] = controlPacketAndFrameData->Control().Opcode();

          const MDArray::ComplexFloatCube frameRawData = controlPacketAndFrameData->Data();
          if (frameRawData.extent(2) != 1)
            m_log << "Warning!! Number of frames not equal to 1 for control packet" << std::endl;

          for (int i_channel = 0; i_channel < numChannels; i_channel++)
            for (int i_readout = 0; i_readout < lenReadout; i_readout++)
              ismrmrd_acq.data(i_readout, i_channel) = frameRawData(i_readout, i_channel, 0);

          //for (int i = 0; i < frameRawData.dimensions(); i++)
          //  m_log << frameRawData.extent(i) << std::endl;
          d.appendAcquisition(ismrmrd_acq);
        } // if (viewValue...)
      } // if (controlPacketAndFrameData->Contrl().Opcode()...)
    } // for (i_control)

    return i_acquisition;
  } // function GERawConverter::appendAcquisitions()

} // namespace OxToIsmrmrd
