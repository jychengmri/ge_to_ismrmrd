
/** @file GenericConverter.cpp */
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

#include "GenericConverter.h"
#include <Orchestra/Legacy/Pfile.h>

namespace PfileToIsmrmrd {

int GenericConverter::get_view_idx(GERecon::Legacy::Pfile *pfile,
        unsigned int view_num, ISMRMRD::EncodingCounters &idx)
{
    // set all the ones we don't care about to zero
    idx.average = 0;
    idx.contrast = 0;
    idx.phase = 0;
    idx.set = 0;
    idx.segment = 0;
    for (int n=0; n<8; n++) {
      idx.user[n] = 0;
    }

    const GERecon::Control::ProcessingControlPointer processingControl(pfile->CreateOrchestraProcessingControl());
    unsigned int nframes = processingControl->Value<int>("AcquiredYRes");

    // Check the pfile header for the data order
    // OLD: if (pfile->dacq_ctrl & PF_RAW_COLLECT) {
    if (pfile->IsRawMode()) {
      // RDB_RAW_COLLECT bit is set, so data is in view order on disk
      // Acquisition looping order is sequence dependent, as has to be
      // implemented per sequence.
      idx.repetition = view_num / (pfile->SliceCount() * nframes);
      view_num = view_num % (pfile->SliceCount() * nframes);

      idx.kspace_encode_step_1 = view_num / pfile->SliceCount();
      view_num = view_num % pfile->SliceCount();

      idx.slice = view_num;
    }
    else {
      // RDB_RAW_COLLECT bit is NOT set, so data is in default ge order on disk
      // Default looping order:
      // repetitionloop (nreps)
      //   sliceloop (SliceCount())
      //     mean baseline (1)
      //       kyloop (nframes)
      idx.repetition = view_num / (pfile->SliceCount() * (1 + nframes));
      // view_num = view_num % (pfile->SliceCount() * (1 + nframes));
      
      // idx.slice = view_num / (1 + nframes);
      // view_num = view_num % (1 + nframes);
      
      // put the frame number in kspace_encode_step_1
      if (view_num < 1) {
        // this is the mean baseline view return -1
        return -1;
      }
      // this is a regular line (frame)
      // idx.kspace_encode_step_1 = view_num - 1;
    }
    
    return 1;
}


boost::shared_ptr<ISMRMRD::NDArray<complex_float_t> > GenericConverter::getKSpaceMatrix(
  GERecon::Legacy::Pfile* pfile, unsigned int i_echo, unsigned int i_phase)
{
  const GERecon::Control::ProcessingControlPointer processingControl(pfile->CreateOrchestraProcessingControl());
  //const GERecon::Legacy::LxDownloadData& downloadData = *pfile->DownloadData();
  //const GERecon::Legacy::RdbHeaderRec& rdbHeader = downloadData.RawHeader();
  
  unsigned int lenFrame = (unsigned int)processingControl->Value<int>("AcquiredXRes");
  unsigned int numViews = (unsigned int)processingControl->Value<int>("AcquiredYRes");
  unsigned int numSlices = (unsigned int)processingControl->Value<int>("AcquiredZRes");
  unsigned int numChannels = pfile->ChannelCount();

  std::vector<size_t> dims = {lenFrame, numViews, numSlices, numChannels};
  boost::shared_ptr<ISMRMRD::NDArray<complex_float_t> > kSpaceMatrix (new ISMRMRD::NDArray<complex_float_t>(dims));
  //const MDArray::FloatVector channelWeights = processingControl->Value<FloatVector>("ChannelWeights");
 
  // Orchestra API provides size in bytes.
  // Pfile is stored as (readout, views, echoes, slice, channel)
  #pragma omp parallel for collapse(2)
  for (unsigned int i_channel = 0; i_channel < numChannels; i_channel++) {
    for (unsigned int i_slice = 0; i_slice < numSlices; i_slice++) {
      MDArray::ComplexFloatMatrix kSpace;
      if (pfile->IsZEncoded()) {  
        auto kSpaceRead = pfile->KSpaceData<float>(
          GERecon::Legacy::Pfile::PassSlicePair(i_phase, i_slice), i_echo, i_channel);
        kSpace.reference(kSpaceRead);
      }
      else {            
        auto kSpaceRead = pfile->KSpaceData<float>(i_slice, i_echo, i_channel, i_phase);
        kSpace.reference(kSpaceRead);
      }

      for (unsigned int i_view = 0; i_view < numViews; i_view++) {
        for (unsigned int i = 0 ; i < lenFrame ; i++)
          (*kSpaceMatrix)(i, i_view, i_slice, i_channel) = kSpace((int)i, (int)i_view);
          
      } // for (i_view)
    } // for (i_slice)
  } // for (i_channel)

  return kSpaceMatrix;

  }

std::vector<ISMRMRD::Acquisition> GenericConverter::getAcquisitions(
        GERecon::Legacy::Pfile* pfile, unsigned int acq_mode)
{
  std::cout << "Getting acquisitions..." << std::endl;
  std::vector<ISMRMRD::Acquisition> acqs;
  
  const GERecon::Control::ProcessingControlPointer processingControl(pfile->CreateOrchestraProcessingControl());
  //const GERecon::Legacy::LxDownloadData& downloadData = *pfile->DownloadData();
  //const GERecon::Legacy::RdbHeaderRec& rdbHeader = downloadData.RawHeader();
  
  int lenFrame = processingControl->Value<int>("AcquiredXRes");
  int numViews = processingControl->Value<int>("AcquiredYRes");
  int numSlices = processingControl->Value<int>("AcquiredZRes");
  int numEchoes = pfile->EchoCount(); //processingControl->Value<int>("NumEchoes");
  int numChannels = pfile->ChannelCount();
  int numPhases = pfile->PhaseCount();
  long long int totalAcquisitions = numSlices * numEchoes * numViews * numPhases;
  //const MDArray::FloatVector channelWeights = processingControl->Value<FloatVector>("ChannelWeights");
 
  std::cout << " Total acquisitions: " << totalAcquisitions << std::endl;
    
  // Make number of acquisitions to be converted
  acqs.resize(totalAcquisitions);
  //int acq_num = 0;

  // Orchestra API provides size in bytes.
  // Pfile is stored as (readout, views, echoes, slice, channel, phase?)
  //#pragma omp parallel for collapse(4)
  for (int i_phase = 0; i_phase < numPhases; i_phase++) {
    for (int i_channel = 0; i_channel < numChannels; i_channel++) {
      std::cout << i_channel << std::endl;
      #pragma omp parallel for collapse(2)
      for (int i_slice = 0; i_slice < numSlices; i_slice++) {
        for (int i_echo = 0; i_echo < numEchoes ; i_echo++) {
          MDArray::ComplexFloatMatrix kSpace;
          if (pfile->IsZEncoded()) {  
            auto kSpaceRead = pfile->KSpaceData<float>(
              GERecon::Legacy::Pfile::PassSlicePair(i_phase, i_slice), i_echo, i_channel);
            kSpace.reference(kSpaceRead);
          }
          else {            
            auto kSpaceRead = pfile->KSpaceData<float>(i_slice, i_echo, i_channel, i_phase);
            kSpace.reference(kSpaceRead);
          }

          for (int i_view = 0; i_view < numViews; i_view++) {
            int acq_num = i_view + numViews * (i_echo + numEchoes * (i_slice + numSlices * i_phase));
            // Grab a reference to the acquisition
            ISMRMRD::Acquisition& acq = acqs.at(acq_num);
            // Set size of this data frame to receive raw data
            acq.resize(lenFrame, numChannels, 0);
            // Need to check that this indexing of kData is correct, and it's not kData(phaseCount, i)
            for (int i = 0 ; i < lenFrame ; i++)
              acq.data(i, i_channel) = kSpace(i, i_view);

            if (i_channel == 0) {
              // Initialize the encoding counters for this acquisition.
              ISMRMRD::EncodingCounters idx;
              get_view_idx(pfile, 0, idx);
          
              idx.contrast  = i_echo;
              idx.kspace_encode_step_1 = i_view;
              idx.phase = i_phase;
              // If 3D acquisition and z encoded, kspace_encode_step_2
              if (processingControl->Value<bool>("Is3DAcquisition") && pfile->IsZEncoded()) {
                idx.kspace_encode_step_2 = i_slice;
                idx.slice = 0;
              } else {
                idx.kspace_encode_step_2 = 0;
                idx.slice = i_slice;
              }
              acq.idx() = idx;
          
              // Fill in the rest of the header
              acq.clearAllFlags();
              acq.measurement_uid() = pfile->RunNumber();
              acq.scan_counter() = acq_num;
              acq.acquisition_time_stamp() = time(NULL); // TODO: can we get a timestamp?
              for (int p = 0; p < ISMRMRD::ISMRMRD_PHYS_STAMPS; p++) {
                acq.physiology_time_stamp()[p] = 0;
              }
              acq.available_channels() = numChannels;
              acq.discard_pre() = 0;
              acq.discard_post() = 0;
              // TODO: TO FIX! (JYCHENG)
              acq.center_sample() = lenFrame/2;
              acq.encoding_space_ref() = 0;
              //acq.sample_time_us() = pfile->sample_time * 1e6;
          
              for (int ch = 0 ; ch < numChannels ; ch++) {
                acq.setChannelActive(ch);
              }
          
              // Patient table off-center
              // TODO: fix the patient table position
              acq.patient_table_position()[0] = 0.0;
              acq.patient_table_position()[1] = 0.0;
              acq.patient_table_position()[2] = 0.0;
          
              // Set first acquisition flag
              if (idx.kspace_encode_step_1 == 0)
                acq.setFlag(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_SLICE);
          
              // Set last acquisition flag
              if (idx.kspace_encode_step_1 == numViews - 1)
                acq.setFlag(ISMRMRD::ISMRMRD_ACQ_LAST_IN_SLICE);
          
              //acq_num++;
            } // if (i_channel == 0)
          } // for (i_view)
        } // for (i_echo)
      } // for (i_slice)
    } // for (i_channel)
  } // for (i_phase)

  return acqs;
}

SEQUENCE_CONVERTER_FACTORY_DECLARE(GenericConverter)

} // namespace PfileToIsmrmrd
