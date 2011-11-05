////////////////////////////////////////////////////////////////////////
/// \file PMTSensitiveDetector.h
//
/// \version $Id: PrimaryParticleInformation.cxx,v 1.3 2009/10/05 23:21:51 t962cvs Exp $
/// \author  bjpjones@mit.edu
////////////////////////////////////////////////////////////////////////
// This is the sensitive detector class for the PMT detectors.  It is
// associated with the relevant detector volumes in DetectorConstruction.cxx
// and is called via the ProcessHits method every time a particle steps
// within the volume.
//
// The detector owns a hit collection which is passed back to LArG4 at
// the end of the event.  One PMTSensitiveDetector corresponds to a set
// of PMT's, which are looked up by their G4PhysicalVolume in the PMTLookup class.
//
// Photons stepping into the volume are stopped and killed and their trackID,
// 4 position and 4 momentum are stored in the relevant PMTHit.
//
// Ben Jones, MIT, 06/04/2010
//


#include <G4VSensitiveDetector.hh>
#include "Simulation/sim.h"

#include "LArG4/PMTLookup.h"

#ifndef PMTSensitiveDetector_h
#define PMTSensitiveDetector_h 1

class G4HCofThisEvent;
class G4TOuchableHistory;
class G4Step;

namespace larg4 {

  class PMTSensitiveDetector : public G4VSensitiveDetector
  {

    
  public:
    PMTSensitiveDetector(G4String name);
    virtual ~PMTSensitiveDetector(){}
    
    
    // Beginning and end of event
    virtual void Initialize(G4HCofThisEvent*);
    virtual void EndOfEvent(G4HCofThisEvent*){}
    
    // Tidy up event in abort
    virtual void clear(){}
    
    // Run per step in sensitive volume
    virtual G4bool ProcessHits( G4Step*, G4TouchableHistory*);
    
    // Return the hit collection generated by this SD
    sim::PMTHitCollection * GetPMTHitCollection();
    
    
    // Required but empty
    virtual void DrawAll(){}
    virtual void PrintAll(){}
    
  private:
    sim::PMTHitCollection  * fThePMTHitCollection;
    PMTLookup              * fThePMTLookup;
    
  };
}

#endif