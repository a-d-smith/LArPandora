// \file PMTResponseAnalyzer.h 
// \author Ben Jones, MIT 2010
//
// Module to determine how many phots have been detected at each PMT
//
// This analyzer takes the PMTHitCollection generated by LArG4's sensitive detectors
// and fills up to four trees in the histograms file.  The four trees are:
//
// PMTEvents       - count how many phots hit the PMT face / were detected across all PMT's per event
// PMTs            - count how many phots hit the PMT face / were detected in each PMT individually for each event
// AllPhotons      - wavelength information for each phot hitting the PMT face
// DetectedPhotons - wavelength information for each phot detected
//
// The user may supply a quantum efficiency and sensitive wavelength range for the PMT's.
// with a QE < 1 and a finite wavelength range, a "detected" phot is one which is
// in the relevant wavelength range and passes the random sampling condition imposed by
// the quantum efficiency of the PMT
//
// PARAMETERS REQUIRED:
// int32   Verbosity          - whether to write to screen a well as to file. levels 0 to 3 specify different levels of detail to display
// string  InputModule        - the module which produced the PMTHitCollection
// bool    MakeAllPhotonsTree - whether to build and store each tree (performance can be enhanced by switching off those not required)
// bool    MakeDetectedPhotonsTree
// bool    MakePMTHitsTree
// bool    MakeEventsTree
// double  QantumEfficiency   - Quantum efficiency of PMT
// double  WavelengthCutLow   - Sensitive wavelength range of PMT
// double  WavelengthCutHigh



#ifndef __CINT__

#include "art/Framework/Core/EDAnalyzer.h"
#include "TTree.h"


#include "TFile.h"

// ROOT includes.
#include <Rtypes.h>
#ifndef PMTResponseAnalyzer_h
#define PMTResponseAnalyzer_h 1


namespace phot {

  class PMTResponseAnalyzer : public art::EDAnalyzer{
    public:
      
      PMTResponseAnalyzer(const fhicl::ParameterSet&);
      virtual ~PMTResponseAnalyzer();
      
      void analyze(art::Event const&);
      
      void beginJob();
      
     
    private:
      
      // Trees to output

      TTree * fThePhotonTreeAll;
      TTree * fThePhotonTreeDetected;
      TTree * fThePMTTree;
      TTree * fTheEventTree;


      // Parameters to read in

      std::string fInputModule;      // Input tag for PMT collection

      int fVerbosity;                // Level of output to write to std::out

      bool fMakeDetectedPhotonsTree; //
      bool fMakeAllPhotonsTree;      //
      bool fMakePMTHitsTree;         // Switches to turn on or off each output
      bool fMakeEventsTree;          //
      
      float fQE;                     // Quantum efficiency of tube

      float fWavelengthCutLow;       // Sensitive wavelength range 
      float fWavelengthCutHigh;      // 


      

      // Data to store in trees

      Float_t fWavelength;
      Float_t fTime;
      Int_t fCount;
      Int_t fCountPMTAll;
      Int_t fCountPMTDetected;

      Int_t fCountEventAll;
      Int_t fCountEventDetected;
      
      Int_t fEventID;
      Int_t fPMTID;
    };
}

#endif

#endif