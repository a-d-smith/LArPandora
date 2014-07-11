/**
 *  @file   LArPandoraInterface/MicroBooNEPandora_module.cc
 *
 *  @brief  lar pandora module.
 *
 */

// Framework Includes
#include "art/Framework/Core/ModuleMacros.h"

// LArSoft includes
#include "RecoBase/Cluster.h"
#include "RecoBase/Track.h"
#include "RecoBase/Shower.h"
#include "RecoBase/SpacePoint.h"

// Local includes
#include "LArPandoraBase.h"

// std includes
#include <string>

//------------------------------------------------------------------------------------------------------------------------------------------

namespace lar_pandora
{

/**
 *  @brief  MicroBooNEPandora class
 */
class MicroBooNEPandora : public LArPandoraBase
{
public:
    /**
     *  @brief  Constructor
     *
     *  @param  pset
     */
    MicroBooNEPandora(fhicl::ParameterSet const &pset);

    /**
     *  @brief  Destructor
     */
    virtual ~MicroBooNEPandora();

private:
    void CreatePandoraGeometry();
    void CreatePandoraHits(const HitVector &hits, HitMap &hitMap) const;
    void CreatePandoraParticles(const ParticleMap &particleMap, const TruthToParticleMap &truthToParticleMap) const;
    void CreatePandoraLinks(const HitMap &hitMap, const HitToParticleMap &hitToParticleMap) const;
    void ProduceArtOutput(art::Event &evt, const HitMap &hitMap) const;

    double m_x0;
    double m_y0;
    double m_z0;

    double m_u0;
    double m_v0;
    double m_w0;
};

DEFINE_ART_MODULE(MicroBooNEPandora)

} // namespace lar_pandora

//------------------------------------------------------------------------------------------------------------------------------------------
// implementation follows

// LArSoft includes
#include "Geometry/Geometry.h"
#include "Utilities/LArProperties.h"
#include "Utilities/DetectorProperties.h"
#include "Utilities/AssociationUtil.h"
#include "SimulationBase/MCTruth.h"
#include "RecoBase/Hit.h"
#include "RecoBase/Cluster.h"
#include "RecoBase/SpacePoint.h"
#include "RecoBase/Track.h"

// Local includes
#include "LArContent.h"
#include "MicroBooNEPseudoLayerCalculator.h"
#include "MicroBooNETransformationCalculator.h"

// System includes
#include <iostream>

namespace lar_pandora {

MicroBooNEPandora::MicroBooNEPandora(fhicl::ParameterSet const &pset) : LArPandoraBase(pset)
{
    produces< std::vector<recob::Track> >();
    produces< std::vector<recob::SpacePoint> >();
    produces< std::vector<recob::Cluster> >();

    produces< art::Assns<recob::Track, recob::SpacePoint> >();
    produces< art::Assns<recob::Track, recob::Cluster> >();
    produces< art::Assns<recob::SpacePoint, recob::Hit> >();
    produces< art::Assns<recob::Cluster, recob::Hit> >();

    m_x0 = 0.0;
    m_y0 = 0.0;
    m_z0 = 0.0;

    m_u0 = 0.0;
    m_v0 = 0.0;
    m_w0 = 0.0;
}

//------------------------------------------------------------------------------------------------------------------------------------------

MicroBooNEPandora::~MicroBooNEPandora()
{

}

//------------------------------------------------------------------------------------------------------------------------------------------

void MicroBooNEPandora::CreatePandoraGeometry()
{
    mf::LogDebug("LArPandora") << " *** MicroBooNEPandora::CreatePandoraGeometry(...) *** " << std::endl;

    // Identify the Geometry and load the calculators
    art::ServiceHandle<geo::Geometry> theGeometry;

    if (theGeometry->DetId() == geo::kMicroBooNE)
    {
	PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, LArContent::SetLArPseudoLayerCalculator(*m_pPandora, new MicroBooNEPseudoLayerCalculator));
	PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, LArContent::SetLArTransformationCalculator(*m_pPandora, new MicroBooNETransformationCalculator));
    }
    else
    {
	mf::LogError("LArPandora") << " Geometry helpers not yet available for detector: " << theGeometry->GetDetectorName() << std::endl;
	throw pandora::StatusCodeException(pandora::STATUS_CODE_INVALID_PARAMETER);
    }

    // Calculate offsets in coordinate system
    // TODO: (1) Find the wireID->Upos and wireID->Vpos methods in LArSoft, (2) Pass this information to geometry helper.
    m_x0 = 0.0;
    m_y0 = 0.0;
    m_z0 = 0.0;

    theGeometry->IntersectionPoint(0, 0, geo::kU,geo::kV, 0, 0, m_y0, m_z0);

    m_u0 = lar::LArGeometryHelper::GetLArTransformationCalculator()->YZtoU(m_y0, m_z0);
    m_v0 = lar::LArGeometryHelper::GetLArTransformationCalculator()->YZtoV(m_y0, m_z0);
    m_w0 = lar::LArGeometryHelper::GetLArPseudoLayerCalculator()->GetZPitch();
}

//------------------------------------------------------------------------------------------------------------------------------------------

void MicroBooNEPandora::CreatePandoraHits(const HitVector &hitVector, HitMap &hitMap) const
{
    mf::LogDebug("LArPandora") << " *** MicroBooNEPandora::CreatePandoraHits(...) *** " << std::endl;

    // TODO: Select hits to be used in reconstruction (e.g. needed for multi-pass reconstruction)

    art::ServiceHandle<util::DetectorProperties> theDetector;
    art::ServiceHandle<util::LArProperties> theLiquidArgon;

    static const double dx_cm(0.5);          // cm
    static const double int_cm(84.0);        // cm
    static const double rad_cm(14.0);        // cm
    static const double dEdX_max(25.0);      // MeV/cm
    static const double dEdX_mip(2.0);       // MeV/cm (for now)
    static const double mips_to_gev(3.5e-4); // from 100 single-electrons

    static const double us_per_tdc(1.0e-3 * theDetector->SamplingRate()); // ns->us
    static const double tdc_offset(theDetector->TriggerOffset());

    static const double wire_pitch_cm(lar::LArGeometryHelper::GetLArPseudoLayerCalculator()->GetZPitch());

    // Loop over hits
    int hitCounter(0);

    for (HitVector::const_iterator iter = hitVector.begin(), iterEnd = hitVector.end(); iter != iterEnd; ++iter)
    {
	const art::Ptr<recob::Hit> hit = *iter;

	const int hit_View(hit->View());
	const double hit_Time(hit->PeakTime());
	const double hit_Charge(hit->Charge(true));
	const double hit_TimeStart(hit->StartTime());
	const double hit_TimeEnd(hit->EndTime());

	const geo::WireID hit_WireID(hit->WireID());

	const double wpos_cm(hit_WireID.Wire * wire_pitch_cm);
	const double xpos_cm(theDetector->ConvertTicksToX(hit_Time, hit_WireID.Plane, hit_WireID.TPC, hit_WireID.Cryostat));
	const double dxpos_cm(theDetector->ConvertTicksToX(hit_TimeEnd, hit_WireID.Plane, hit_WireID.TPC, hit_WireID.Cryostat) -
			      theDetector->ConvertTicksToX(hit_TimeStart, hit_WireID.Plane, hit_WireID.TPC, hit_WireID.Cryostat));

	const double t_us((hit_Time - tdc_offset) * us_per_tdc);
	const double dQdX(hit_Charge / wire_pitch_cm); // ADC/cm
	const double dQdX_e(dQdX / (theDetector->ElectronsToADC() * exp(-t_us / theLiquidArgon->ElectronLifetime()))); // e/cm

	double dEdX(theLiquidArgon->BirksCorrection(dQdX_e));

	if ((dEdX < 0) || (dEdX > dEdX_max))
	    dEdX = dEdX_max;

	const double mips(dEdX / dEdX_mip); // TODO: Check if calibration procedure is correct

	hitMap[++hitCounter] = hit;

	// Create Pandora CaloHit
	PandoraApi::CaloHit::Parameters caloHitParameters;
	caloHitParameters.m_expectedDirection = pandora::CartesianVector(0., 0., 1.);
	caloHitParameters.m_cellNormalVector = pandora::CartesianVector(0., 0., 1.);
	caloHitParameters.m_cellSizeU = dx_cm;
	caloHitParameters.m_cellSizeV = dxpos_cm; // Or the nominal dx_cm
	caloHitParameters.m_cellThickness = wire_pitch_cm;
	caloHitParameters.m_time = 0.;
	caloHitParameters.m_nCellRadiationLengths = dx_cm / rad_cm;
	caloHitParameters.m_nCellInteractionLengths = dx_cm / int_cm;
	caloHitParameters.m_isDigital = false;
	caloHitParameters.m_detectorRegion = pandora::ENDCAP;
	caloHitParameters.m_layer = 0;
	caloHitParameters.m_isInOuterSamplingLayer = false;
	caloHitParameters.m_inputEnergy = hit_Charge;
	caloHitParameters.m_mipEquivalentEnergy = mips;
	caloHitParameters.m_electromagneticEnergy = mips * mips_to_gev;
	caloHitParameters.m_hadronicEnergy = mips * mips_to_gev;
	caloHitParameters.m_pParentAddress = (void*)((intptr_t)hitCounter);

	if (hit_View == geo::kW)
	{
	    caloHitParameters.m_hitType = pandora::TPC_VIEW_W;
	    caloHitParameters.m_positionVector = pandora::CartesianVector(xpos_cm + m_x0, 0., wpos_cm + m_w0);
	}
	else if(hit_View == geo::kU)
	{
	    caloHitParameters.m_hitType = pandora::TPC_VIEW_U;
	    caloHitParameters.m_positionVector = pandora::CartesianVector(xpos_cm + m_x0, 0., wpos_cm + m_u0);
	}
	else if(hit_View == geo::kV)
	{
	    caloHitParameters.m_hitType = pandora::TPC_VIEW_V;
	    caloHitParameters.m_positionVector = pandora::CartesianVector(xpos_cm + m_x0, 0., wpos_cm + m_v0);
	}
	else
	{
	    mf::LogError("LArPandora") << " --- WARNING: UNKNOWN VIEW !!!  (View=" << hit_View << ")" << std::endl;
	    throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);
	}

	if (std::isnan(mips))
	{
	    mf::LogError("LArPandora") << " --- WARNING: UNPHYSICAL PULSEHEIGHT !!! (MIPs=" << mips << ")" << std::endl;
	    throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);
	}

	PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::CaloHit::Create(*m_pPandora, caloHitParameters));
    }

    mf::LogDebug("LArPandora") << "   Number of Pandora hits: " << hitCounter << std::endl;
}

//------------------------------------------------------------------------------------------------------------------------------------------

void MicroBooNEPandora::CreatePandoraParticles(const ParticleMap& particleMap, const TruthToParticleMap &truthToParticleMap) const
{
    mf::LogDebug("LArPandora") << " *** MicroBooNEPandora::CreatePandoraParticles(...) *** " << std::endl;
    static const int id_offset = 100000000;

    // Loop over incident particles
    int neutrinoCounter(0);

    for (TruthToParticleMap::const_iterator iter = truthToParticleMap.begin(), iterEnd = truthToParticleMap.end(); iter != iterEnd; ++iter)
    {
	const art::Ptr<simb::MCTruth> truth = iter->first;

	if (truth->NeutrinoSet())
	{
	    const simb::MCNeutrino neutrino(truth->GetNeutrino());
	    ++neutrinoCounter;

	    if (neutrinoCounter > id_offset)
		throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

	    const int neutrinoID(neutrinoCounter + 4 * id_offset);

	    // Create Pandora 3D MC Particle
	    PandoraApi::MCParticle::Parameters mcParticleParameters;
	    mcParticleParameters.m_energy = neutrino.Nu().E();
	    mcParticleParameters.m_momentum = pandora::CartesianVector(neutrino.Nu().Px(), neutrino.Nu().Py(), neutrino.Nu().Pz());
	    mcParticleParameters.m_vertex = pandora::CartesianVector(neutrino.Nu().Vx(), neutrino.Nu().Vy(), neutrino.Nu().Vz());
	    mcParticleParameters.m_endpoint = pandora::CartesianVector(neutrino.Nu().Vx(), neutrino.Nu().Vy(), neutrino.Nu().Vz());
	    mcParticleParameters.m_particleId = neutrino.Nu().PdgCode();
	    mcParticleParameters.m_mcParticleType = pandora::MC_STANDARD;
	    mcParticleParameters.m_pParentAddress = (void*)((intptr_t)neutrinoID);
	    PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*m_pPandora, mcParticleParameters));

	    // Loop over associated particles
	    const std::vector<int> particleCollection = iter->second;

	    for (unsigned int j = 0; j < particleCollection.size(); ++j)
	    {
		const int trackID = particleCollection.at(j);

		ParticleMap::const_iterator iterJ = particleMap.find(trackID);

		if( iterJ == particleMap.end() )
		    throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

		const art::Ptr<simb::MCParticle> particle = iterJ->second;

		// Mother/Daughter Links
		if (particle->Mother() == 0)
		    PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::SetMCParentDaughterRelationship(*m_pPandora,
			(void*)((intptr_t)neutrinoID), (void*)((intptr_t)trackID)));
	    }
	}
    }

    mf::LogDebug("LArPandora") << "   Number of Pandora neutrinos: " << neutrinoCounter << std::endl;

    // Loop over G4 particles
    int particleCounter(0);

    for (ParticleMap::const_iterator iterI = particleMap.begin(), iterEndI = particleMap.end(); iterI != iterEndI; ++iterI )
    {
	const art::Ptr<simb::MCParticle> particle = iterI->second;

	if (particle->TrackId() != iterI->first)
	    throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

	if (particle->TrackId() > id_offset)
	    throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

	++particleCounter;

	// Find Start and End Points
	int startT(0), endT(0);
	this->GetStartAndEndPoints(particle, startT, endT);

	const float vtxX(particle->Vx(startT));
	const float vtxY(particle->Vy(startT));
	const float vtxZ(particle->Vz(startT));

	const float endX(particle->Vx(endT));
	const float endY(particle->Vy(endT));
	const float endZ(particle->Vz(endT));

	const float pX(particle->Px(startT));
	const float pY(particle->Py(startT));
	const float pZ(particle->Pz(startT));
	const float E(particle->E(startT));

	// Create 3D Pandora MC Particle
	PandoraApi::MCParticle::Parameters mcParticleParameters;
	mcParticleParameters.m_energy = E;
	mcParticleParameters.m_particleId = particle->PdgCode();
	mcParticleParameters.m_momentum = pandora::CartesianVector(pX, pY, pZ);
	mcParticleParameters.m_vertex = pandora::CartesianVector(vtxX, vtxY, vtxZ);
	mcParticleParameters.m_endpoint = pandora::CartesianVector(endX, endY, endZ);
	mcParticleParameters.m_mcParticleType = pandora::MC_STANDARD;
	mcParticleParameters.m_pParentAddress = (void*)((intptr_t)particle->TrackId());
	PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*m_pPandora, mcParticleParameters));

	// Create Mother/Daughter Links between 3D MC Particles
	const int id_mother(particle->Mother());
	ParticleMap::const_iterator iterJ = particleMap.find(id_mother);

	if (iterJ != particleMap.end())
	    PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::SetMCParentDaughterRelationship(*m_pPandora,
		(void*)((intptr_t)id_mother), (void*)((intptr_t)particle->TrackId())));


	// Create 2D Pandora MC Particles for Event Display
	const float dx(endX - vtxX);
	const float dy(endY - vtxY);
	const float dz(endZ - vtxZ);
	const float dw(lar::LArGeometryHelper::GetLArPseudoLayerCalculator()->GetZPitch());

	if (dx * dx + dy * dy + dz * dz < 0.5 * dw * dw)
	    continue;

	// Create U projection
	mcParticleParameters.m_momentum = pandora::CartesianVector(pX, 0.f,
	    lar::LArGeometryHelper::GetLArTransformationCalculator()->PYPZtoPU(pY, pZ));
	mcParticleParameters.m_vertex = pandora::CartesianVector(vtxX, 0.f,
	    lar::LArGeometryHelper::GetLArTransformationCalculator()->YZtoU(vtxY, vtxZ));
	mcParticleParameters.m_endpoint = pandora::CartesianVector(endX,  0.f,
	    lar::LArGeometryHelper::GetLArTransformationCalculator()->YZtoU(endY, endZ));
	mcParticleParameters.m_mcParticleType = pandora::MC_VIEW_U;
	mcParticleParameters.m_pParentAddress = (void*)((intptr_t)(particle->TrackId() + 1 * id_offset));
	PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*m_pPandora, mcParticleParameters));

	// Create V projection
	mcParticleParameters.m_momentum = pandora::CartesianVector(pX, 0.f,
	    lar::LArGeometryHelper::GetLArTransformationCalculator()->PYPZtoPV(pY, pZ));
	mcParticleParameters.m_vertex = pandora::CartesianVector(vtxX, 0.f,
	    lar::LArGeometryHelper::GetLArTransformationCalculator()->YZtoV(vtxY, vtxZ));
	mcParticleParameters.m_endpoint = pandora::CartesianVector(endX,  0.f,
	    lar::LArGeometryHelper::GetLArTransformationCalculator()->YZtoV(endY, endZ));
	mcParticleParameters.m_mcParticleType = pandora::MC_VIEW_V;
	mcParticleParameters.m_pParentAddress = (void*)((intptr_t)(particle->TrackId() + 2 * id_offset));
	PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*m_pPandora, mcParticleParameters));

	// Create W projection
	mcParticleParameters.m_momentum = pandora::CartesianVector(pX, 0.f, pZ);
	mcParticleParameters.m_vertex = pandora::CartesianVector(vtxX, 0.f, vtxZ);
	mcParticleParameters.m_endpoint = pandora::CartesianVector(endX,  0.f, endZ);
	mcParticleParameters.m_mcParticleType = pandora::MC_VIEW_W;
	mcParticleParameters.m_pParentAddress = (void*)((intptr_t)(particle->TrackId() + 3 * id_offset));
	PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*m_pPandora, mcParticleParameters));
    }

    mf::LogDebug("LArPandora") << "   Number of Pandora particles: " << particleCounter << std::endl;
}

//------------------------------------------------------------------------------------------------------------------------------------------

void MicroBooNEPandora::CreatePandoraLinks(const HitMap &hitMap, const HitToParticleMap &hitToParticleMap) const
{
    mf::LogDebug("LArPandora") << " *** MicroBooNEPandora::CreatePandoraLinks(...) *** " << std::endl;

    for (HitMap::const_iterator iterI = hitMap.begin(), iterEndI = hitMap.end(); iterI != iterEndI ; ++iterI)
    {
	const int hitID(iterI->first);
	const art::Ptr<recob::Hit> hit(iterI->second);

	HitToParticleMap::const_iterator iterJ = hitToParticleMap.find(hit);

	if (iterJ == hitToParticleMap.end())
	    continue;

	std::vector<cheat::TrackIDE> trackCollection = iterJ->second;

	if (trackCollection.size() == 0)
	    throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

	for (unsigned int k = 0; k < trackCollection.size(); ++k)
	{
	    const cheat::TrackIDE trackIDE(trackCollection.at(k));
	    const int trackID(std::abs(trackIDE.trackID)); // TODO: Find out why std::abs is needed
	    const float energyFrac(trackIDE.energyFrac);

	    PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::SetCaloHitToMCParticleRelationship(*m_pPandora,
		(void*)((intptr_t)hitID), (void*)((intptr_t)trackID), energyFrac));
	}
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void MicroBooNEPandora::ProduceArtOutput(art::Event &evt, const HitMap &hitMap) const
{
    mf::LogDebug("LArPandora") << " *** MicroBooNEPandora::ProduceArtOutput() *** " << std::endl;

    // Get list of Pandora particles (ATTN: assume that all reco particles live in curent list)
    const pandora::PfoList *pPfoList = NULL;
    PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::GetCurrentPfoList(*m_pPandora, pPfoList));

    if (NULL == pPfoList)
    {
	mf::LogDebug("LArPandora") << "   No reconstructed particles for this event [return] " << std::endl;
	return;
    }

    pandora::PfoVector pfoVector;
    for (pandora::PfoList::const_iterator iter = pPfoList->begin(), iterEnd = pPfoList->end(); iter != iterEnd; ++iter)
	pfoVector.push_back(*iter);

    std::sort(pfoVector.begin(), pfoVector.end(), lar::LArPfoHelper::SortByNHits);

    // Set up ART outputs
    std::unique_ptr< std::vector<recob::Track> >      outputTracks( new std::vector<recob::Track> );
    std::unique_ptr< std::vector<recob::SpacePoint> > outputSpacePoints( new std::vector<recob::SpacePoint> );
    std::unique_ptr< std::vector<recob::Cluster> >    outputClusters( new std::vector<recob::Cluster> );

    std::unique_ptr< art::Assns<recob::Track, recob::SpacePoint> > outputTracksToSpacePoints( new art::Assns<recob::Track, recob::SpacePoint> );
    std::unique_ptr< art::Assns<recob::Track, recob::Cluster> >    outputTracksToClusters( new art::Assns<recob::Track, recob::Cluster> );
    std::unique_ptr< art::Assns<recob::SpacePoint, recob::Hit> >   outputSpacePointsToHits( new art::Assns<recob::SpacePoint, recob::Hit> );
    std::unique_ptr< art::Assns<recob::Cluster, recob::Hit> >      outputClustersToHits( new art::Assns<recob::Cluster, recob::Hit> );

    typedef std::map< const pandora::CaloHit*, TVector3 > ThreeDHitMap;
    typedef std::vector< TVector3 > ThreeDHitVector;

    // Loop over Pandora particles
    for (pandora::PfoVector::iterator pIter = pfoVector.begin(), pIterEnd = pfoVector.end(); pIter != pIterEnd; ++pIter)
    {
	const pandora::ParticleFlowObject *const pPfo = *pIter;
	const pandora::ClusterList &pfoClusterList = pPfo->GetClusterList();

	// Get Pandora 3D Hits
	pandora::CaloHitList pandoraHitList3D;
	for (pandora::ClusterList::const_iterator cIter = pfoClusterList.begin(), cIterEnd = pfoClusterList.end(); cIter != cIterEnd; ++cIter)
	{
	    const pandora::Cluster *const pCluster = *cIter;

	    if (pandora::TPC_3D != lar::LArClusterHelper::GetClusterHitType(pCluster))
		continue;

	    pCluster->GetOrderedCaloHitList().GetCaloHitList(pandoraHitList3D);
	}

	// Get Pandora 3D Positions
	ThreeDHitMap spacePointMap;
	ThreeDHitVector spacePointList, spacePointErrorList;
	for (pandora::CaloHitList::const_iterator hIter = pandoraHitList3D.begin(), hIterEnd = pandoraHitList3D.end(); hIter != hIterEnd; ++hIter)
	{
	    const pandora::CaloHit *const pCaloHit3D = *hIter;

	    if (pandora::TPC_3D != pCaloHit3D->GetHitType())
		throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

	    const double xpos_cm(pCaloHit3D->GetPositionVector().GetX() - m_x0);
	    const double ypos_cm(pCaloHit3D->GetPositionVector().GetY() - m_y0);
	    const double zpos_cm(pCaloHit3D->GetPositionVector().GetZ() - m_z0);

	    TVector3 xyz(xpos_cm, ypos_cm, zpos_cm);
	    TVector3 dxdydz(0.0, 0.0, 0.0); // TODO: Fill in representative errors

	    spacePointList.push_back(xyz);
	    spacePointErrorList.push_back(dxdydz);

	    spacePointMap.insert( std::pair<const pandora::CaloHit* const, TVector3>(pCaloHit3D, xyz) );
	}

	// Step 0: Track or shower?
	bool foundTrack(true && !pandoraHitList3D.empty()); // TODO: Need Pandora flag for track or shower

	// Step 1: Build Track or Shower
	if (foundTrack)
	{
	    recob::Track newTrack(spacePointList, spacePointErrorList);
	    outputTracks->push_back(newTrack);
	}

	// Step 2: Build Space Points
	for (pandora::CaloHitList::const_iterator hIter = pandoraHitList3D.begin(), hIterEnd = pandoraHitList3D.end(); hIter != hIterEnd; ++hIter)
	{
	    const pandora::CaloHit *const pCaloHit3D = *hIter;
	    const pandora::CaloHit *const pCaloHit2D = static_cast<const pandora::CaloHit*>(pCaloHit3D->GetParentCaloHitAddress());

	    const void *pHitAddress(pCaloHit2D->GetParentCaloHitAddress());
	    const intptr_t hitID_temp((intptr_t)(pHitAddress)); // TODO
	    const int hitID((int)(hitID_temp));

	    HitMap::const_iterator artIter = hitMap.find(hitID);

	    if (artIter == hitMap.end())
		throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

	    HitVector hitVector;
	    art::Ptr<recob::Hit> hit = artIter->second;
	    hitVector.push_back(hit);

	    ThreeDHitMap::const_iterator vIter = spacePointMap.find(pCaloHit3D);
	    if (spacePointMap.end() == vIter)
		throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

	    TVector3 point(vIter->second);

	    double xyz[3] = { point.x(), point.y(), point.z() };
	    double dxdydz[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 }; // TODO: Fill in representative errors
	    double chi2(0.0);

	    recob::SpacePoint newSpacePoint(xyz, dxdydz, chi2);
	    outputSpacePoints->push_back(newSpacePoint);

	    util::CreateAssn(*this, evt, *(outputSpacePoints.get()), hitVector, *(outputSpacePointsToHits.get()));

	    if (foundTrack)
		util::CreateAssn(*this, evt, *(outputTracks.get()), *(outputSpacePoints.get()), *(outputTracksToSpacePoints.get()),
		    outputSpacePoints->size() - 1, outputSpacePoints->size());
	}

	// Step 3: Build Clusters
	for (pandora::ClusterList::const_iterator cIter = pfoClusterList.begin(), cIterEnd = pfoClusterList.end(); cIter != cIterEnd; ++cIter)
	{
	    const pandora::Cluster *const pCluster = *cIter;

	    if (pandora::TPC_3D == lar::LArClusterHelper::GetClusterHitType(pCluster))
		continue;

	    pandora::CaloHitList pandoraHitList2D;
	    pCluster->GetOrderedCaloHitList().GetCaloHitList(pandoraHitList2D);

	    HitVector hitVector;
	    for (pandora::CaloHitList::const_iterator hIter = pandoraHitList2D.begin(), hIterEnd = pandoraHitList2D.end(); hIter != hIterEnd; ++hIter)
	    {
		const pandora::CaloHit *const pCaloHit = *hIter;

		const void *pHitAddress(pCaloHit->GetParentCaloHitAddress());
		const intptr_t hitID_temp((intptr_t)(pHitAddress)); // TODO
		const int hitID((int)(hitID_temp));

		HitMap::const_iterator artIter = hitMap.find(hitID);

		if (artIter == hitMap.end())
		    throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

		art::Ptr<recob::Hit> hit = artIter->second;
		hitVector.push_back(hit);
	    }

	    if (hitVector.empty())
		throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

	    recob::Cluster newCluster;
	    outputClusters->push_back(newCluster);

	    util::CreateAssn(*this, evt, *(outputClusters.get()), hitVector, *(outputClustersToHits.get()));

	    if (foundTrack)
		util::CreateAssn(*this, evt, *(outputTracks.get()), *(outputClusters.get()), *(outputTracksToClusters.get()),
		    outputClusters->size() - 1, outputClusters->size());
	}
    }

    evt.put(std::move(outputTracks));
    evt.put(std::move(outputSpacePoints));
    evt.put(std::move(outputClusters));

    evt.put(std::move(outputTracksToSpacePoints));
    evt.put(std::move(outputTracksToClusters));
    evt.put(std::move(outputSpacePointsToHits));
    evt.put(std::move(outputClustersToHits));
}

} // namespace lar_pandora
