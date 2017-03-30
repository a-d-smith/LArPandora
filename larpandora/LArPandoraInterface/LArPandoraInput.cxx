/**
 *  @file   larpandora/LArPandoraInterface/LArPandoraInput.cxx
 *
 *  @brief  Helper functions for providing inputs to pandora
 */

#include "larcore/Geometry/Geometry.h"
#include "larcore/Geometry/TPCGeo.h"
#include "larcore/Geometry/PlaneGeo.h"
#include "larcore/Geometry/WireGeo.h"
#include "larcoreobj/SimpleTypesAndConstants/RawTypes.h"

#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RecoBase/PFParticle.h"
#include "lardataobj/RecoBase/Seed.h"
#include "lardataobj/RecoBase/Shower.h"
#include "lardataobj/RecoBase/SpacePoint.h"
#include "lardataobj/RecoBase/Vertex.h"

#include "larevt/CalibrationDBI/Interface/ChannelStatusService.h"
#include "larevt/CalibrationDBI/Interface/ChannelStatusProvider.h"

#include "nusimdata/SimulationBase/MCTruth.h"

#include "lardata/DetectorInfoServices/DetectorClocksService.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "lardata/DetectorInfoServices/LArPropertiesService.h"

#include "Api/PandoraApi.h"

#include "larpandoracontent/LArHelpers/LArGeometryHelper.h"
#include "larpandoracontent/LArObjects/LArMCParticle.h"
#include "larpandoracontent/LArStitching/MultiPandoraApi.h"
#include "larpandoracontent/LArPlugins/LArTransformationPlugin.h"

#include "larpandora/LArPandoraInterface/ILArPandora.h"
#include "larpandora/LArPandoraInterface/LArPandoraInput.h"

namespace lar_pandora
{

void LArPandoraInput::CreatePandoraHits2D(const Settings &settings, const LArDriftVolumeMap &driftVolumeMap, const HitVector &hitVector, IdToHitMap &idToHitMap)
{
    mf::LogDebug("LArPandora") << " *** LArPandoraInput::CreatePandoraHits2D(...) *** " << std::endl;

    if (!settings.m_pPrimaryPandora)
        throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraHits2D --- primary Pandora instance does not exist ";

    // Set up ART services
    art::ServiceHandle<geo::Geometry> theGeometry;
    auto const* theDetector = lar::providerFrom<detinfo::DetectorPropertiesService>();

    // Loop over ART hits
    int hitCounter(0);

    for (HitVector::const_iterator iter = hitVector.begin(), iterEnd = hitVector.end(); iter != iterEnd; ++iter)
    {
        const art::Ptr<recob::Hit> hit = *iter;
        const geo::WireID hit_WireID(hit->WireID());
        const pandora::Pandora *pPandora(nullptr);

        try
        {
            const int volumeID(LArPandoraGeometry::GetVolumeID(driftVolumeMap, hit_WireID.Cryostat, hit_WireID.TPC));
            pPandora = MultiPandoraApi::GetDaughterPandoraInstance(settings.m_pPrimaryPandora, volumeID);
        }
        catch (pandora::StatusCodeException &)
        {
        }

        if (!pPandora)
            continue;

        // Get basic hit properties (view, time, charge)
        const geo::View_t hit_View(hit->View());
        const double hit_Charge(hit->Integral());
        const double hit_Time(hit->PeakTime());
        const double hit_TimeStart(hit->PeakTimeMinusRMS());
        const double hit_TimeEnd(hit->PeakTimePlusRMS());

        // Get hit X coordinate and, if using a single global drift volume, remove any out-of-time hits here
        const double xpos_cm(theDetector->ConvertTicksToX(hit_Time, hit_WireID.Plane, hit_WireID.TPC, hit_WireID.Cryostat));
        const double dxpos_cm(std::fabs(theDetector->ConvertTicksToX(hit_TimeEnd, hit_WireID.Plane, hit_WireID.TPC, hit_WireID.Cryostat) -
            theDetector->ConvertTicksToX(hit_TimeStart, hit_WireID.Plane, hit_WireID.TPC, hit_WireID.Cryostat)));

        if (settings.m_truncateReadout)
        {
            const geo::TPCGeo &theTpc(theGeometry->TPC(hit_WireID.TPC, hit_WireID.Cryostat));
            double localCoord[3] = {0.,0.,0.};
            double worldCoord[3] = {0.,0.,0.};
            theTpc.LocalToWorld(localCoord, worldCoord);

            const double drift_min_xpos_cm(worldCoord[0] - theTpc.ActiveHalfWidth());
            const double drift_max_xpos_cm(worldCoord[0] + theTpc.ActiveHalfWidth());

            if (xpos_cm < drift_min_xpos_cm || xpos_cm > drift_max_xpos_cm)
                continue;
        }

        // Get hit Y and Z coordinates, based on central position of wire
        double xyz[3];
        theGeometry->Cryostat(hit_WireID.Cryostat).TPC(hit_WireID.TPC).Plane(hit_WireID.Plane).Wire(hit_WireID.Wire).GetCenter(xyz);
        const double y0_cm(xyz[1]);
        const double z0_cm(xyz[2]);

        // Get other hit properties here
        const double wire_pitch_cm(theGeometry->WirePitch(hit_View)); // cm
        const double mips(LArPandoraInput::GetMips(settings, hit_Charge, hit_View));

        // Create Pandora CaloHit
        PandoraApi::CaloHit::Parameters caloHitParameters;
        caloHitParameters.m_expectedDirection = pandora::CartesianVector(0., 0., 1.);
        caloHitParameters.m_cellNormalVector = pandora::CartesianVector(0., 0., 1.);
        caloHitParameters.m_cellSize0 = settings.m_dx_cm;
        caloHitParameters.m_cellSize1 = (settings.m_useHitWidths ? dxpos_cm : settings.m_dx_cm);
        caloHitParameters.m_cellThickness = wire_pitch_cm;
        caloHitParameters.m_cellGeometry = pandora::RECTANGULAR;
        caloHitParameters.m_time = 0.;
        caloHitParameters.m_nCellRadiationLengths = settings.m_dx_cm / settings.m_rad_cm;
        caloHitParameters.m_nCellInteractionLengths = settings.m_dx_cm / settings.m_int_cm;
        caloHitParameters.m_isDigital = false;
        caloHitParameters.m_hitRegion = pandora::SINGLE_REGION;
        caloHitParameters.m_layer = 0;
        caloHitParameters.m_isInOuterSamplingLayer = false;
        caloHitParameters.m_inputEnergy = hit_Charge;
        caloHitParameters.m_mipEquivalentEnergy = mips;
        caloHitParameters.m_electromagneticEnergy = mips * settings.m_mips_to_gev;
        caloHitParameters.m_hadronicEnergy = mips * settings.m_mips_to_gev;
        caloHitParameters.m_pParentAddress = (void*)((intptr_t)(++hitCounter));

        const geo::View_t pandora_View(settings.m_globalViews ? LArPandoraGeometry::GetGlobalView(hit_WireID.Cryostat, hit_WireID.TPC, hit_View) : hit_View);

        if (pandora_View == geo::kW)
        {
            caloHitParameters.m_hitType = pandora::TPC_VIEW_W;
            const double wpos_cm(z0_cm);
            caloHitParameters.m_positionVector = pandora::CartesianVector(xpos_cm, 0., wpos_cm);
        }
        else if(pandora_View == geo::kU)
        {
            caloHitParameters.m_hitType = pandora::TPC_VIEW_U;
            const double upos_cm(lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoU(y0_cm, z0_cm));
            caloHitParameters.m_positionVector = pandora::CartesianVector(xpos_cm, 0., upos_cm);
        }
        else if(pandora_View == geo::kV)
        {
            caloHitParameters.m_hitType = pandora::TPC_VIEW_V;
            const double vpos_cm(lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoV(y0_cm, z0_cm));
            caloHitParameters.m_positionVector = pandora::CartesianVector(xpos_cm, 0., vpos_cm);
        }
        else
        {
            mf::LogError("LArPandora") << " --- WARNING: UNKNOWN VIEW !!!  (View=" << hit_View << ")" << std::endl;
            throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraHits2D --- this wire view not recognised! ";
        }

        // Check for unphysical pulse heights
        if (std::isnan(mips))
        {
            mf::LogError("LArPandora") << " --- WARNING: UNPHYSICAL PULSEHEIGHT !!! (MIPs=" << mips << ")" << std::endl;
            throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraHits2D --- this pulse height is unphysical! ";
        }

        // Store the hit address
        if (hitCounter >= settings.m_uidOffset)
        {
            mf::LogError("LArPandora") << " --- WARNING: TOO MANY HITS !!! (hitCounter=" << hitCounter << ")" << std::endl;
            throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraHits2D --- detected an excessive number of hits! (" << hitCounter << ") ";
        }

        idToHitMap[hitCounter] = hit;

        // Create the Pandora hit
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::CaloHit::Create(*pPandora, caloHitParameters));
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArPandoraInput::CreatePandoraReadoutGaps(const Settings &settings, const LArDriftVolumeMap &driftVolumeMap)
{
    mf::LogDebug("LArPandora") << " *** LArPandoraInput::CreatePandoraReadoutGaps(...) *** " << std::endl;

    if (!settings.m_pPrimaryPandora)
        throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraReadoutGaps --- primary Pandora instance does not exist ";

    art::ServiceHandle<geo::Geometry> theGeometry;
    const lariov::ChannelStatusProvider &channelStatus(art::ServiceHandle<lariov::ChannelStatusService>()->GetProvider());

    for (unsigned int icstat = 0; icstat < theGeometry->Ncryostats(); ++icstat)
    {
        for (unsigned int itpc = 0; itpc < theGeometry->NTPC(icstat); ++itpc)
        {
            const geo::TPCGeo &TPC(theGeometry->TPC(itpc));
            const pandora::Pandora *pPandora(nullptr);

            try
            {
                const int volumeID(LArPandoraGeometry::GetVolumeID(driftVolumeMap, icstat,  itpc));
                pPandora = MultiPandoraApi::GetDaughterPandoraInstance(settings.m_pPrimaryPandora, volumeID);
            }
            catch (pandora::StatusCodeException &)
            {
            }

            if (!pPandora)
                continue;

            for (unsigned int iplane = 0; iplane < TPC.Nplanes(); ++iplane)
            {
                const geo::PlaneGeo &plane(TPC.Plane(iplane));
                const float halfWirePitch(0.5f * theGeometry->WirePitch(plane.View()));
                const unsigned int nWires(theGeometry->Nwires(geo::PlaneID(icstat, itpc, plane.View())));

                int firstBadWire(-1), lastBadWire(-1);

                for (unsigned int iwire = 0; iwire < nWires; ++iwire)
                {
                    const raw::ChannelID_t channel(theGeometry->PlaneWireToChannel(plane.View(), iwire, itpc, icstat));
                    const bool isBadChannel(channelStatus.IsBad(channel));
                    const bool isLastWire(nWires == (iwire + 1));

                    if (isBadChannel && (firstBadWire < 0))
                        firstBadWire = iwire;

                    if (isBadChannel || isLastWire)
                        lastBadWire = iwire;

                    if (isBadChannel && !isLastWire)
                        continue;

                    if ((firstBadWire < 0) || (lastBadWire < 0))
                        continue;

                    double firstXYZ[3], lastXYZ[3];
                    theGeometry->Cryostat(icstat).TPC(itpc).Plane(iplane).Wire(firstBadWire).GetCenter(firstXYZ);
                    theGeometry->Cryostat(icstat).TPC(itpc).Plane(iplane).Wire(lastBadWire).GetCenter(lastXYZ);

                    PandoraApi::Geometry::LineGap::Parameters parameters;

                    const geo::View_t iview = (geo::View_t)iplane;
                    const geo::View_t pandoraView(settings.m_globalViews ? LArPandoraGeometry::GetGlobalView(icstat, itpc, iview) : iview);

                    if (pandoraView == geo::kW)
                    {
                        const float firstW(firstXYZ[2]);
                        const float lastW(lastXYZ[2]);

                        parameters.m_hitType = pandora::TPC_VIEW_W;
                        parameters.m_lineStartZ = std::min(firstW, lastW) - halfWirePitch;
                        parameters.m_lineEndZ = std::max(firstW, lastW) + halfWirePitch;
                    }
                    else if (pandoraView == geo::kU)
                    {
                        const float firstU(lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoU(firstXYZ[1], firstXYZ[2]));
                        const float lastU(lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoU(lastXYZ[1], lastXYZ[2]));

                        parameters.m_hitType = pandora::TPC_VIEW_U;
                        parameters.m_lineStartZ = std::min(firstU, lastU) - halfWirePitch;
                        parameters.m_lineEndZ = std::max(firstU, lastU) + halfWirePitch;
                    }
                    else if (pandoraView == geo::kV)
                    {
                        const float firstV(lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoV(firstXYZ[1], firstXYZ[2]));
                        const float lastV(lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoV(lastXYZ[1], lastXYZ[2]));

                        parameters.m_hitType = pandora::TPC_VIEW_V;
                        parameters.m_lineStartZ = std::min(firstV, lastV) - halfWirePitch;
                        parameters.m_lineEndZ = std::max(firstV, lastV) + halfWirePitch;
                    }

                    PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::Geometry::LineGap::Create(*pPandora, parameters));
                    firstBadWire = -1; lastBadWire = -1;
                }
            }
        }
    }
}
//------------------------------------------------------------------------------------------------------------------------------------------

void LArPandoraInput::CreatePandoraDetectorGaps(const Settings &settings, const LArDetectorGapList &listOfGaps)
{
    mf::LogDebug("LArPandora") << " *** LArPandoraInput::CreatePandoraDetectorGaps(...) *** " << std::endl;

    if (!settings.m_pPrimaryPandora)
        throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraDetectorGaps --- primary Pandora instance does not exist ";

    // TODO - Extend LineGap objects to cover these types of gaps! For now, just print them to screen...

    for (const LArDetectorGap &nextGap : listOfGaps)
    {
        mf::LogDebug("LArPandora") << " NEXT GAP - X1=" << nextGap.GetX1() << ", X2=" << nextGap.GetX2() << std::endl
                                   << "            Y1=" << nextGap.GetY1() << ", Y2=" << nextGap.GetY2() << std::endl
                                     << "            Z1=" << nextGap.GetZ1() << ", Z2=" << nextGap.GetZ2() << std::endl;
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArPandoraInput::CreatePandoraMCParticles(const Settings &settings, const LArDriftVolumeMap &driftVolumeMap,
    const MCTruthToMCParticles &truthToParticleMap, const MCParticlesToMCTruth &particleToTruthMap)
{
    mf::LogDebug("LArPandora") << " *** LArPandoraInput::CreatePandoraMCParticles(...) *** " << std::endl;

    if (!settings.m_pPrimaryPandora)
        throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraMCParticles --- primary Pandora instance does not exist ";

    PandoraInstanceList pandoraInstanceList(MultiPandoraApi::GetDaughterPandoraInstanceList(settings.m_pPrimaryPandora));

    if (pandoraInstanceList.empty())
        pandoraInstanceList.push_back(settings.m_pPrimaryPandora);

    // Make indexed list of MC particles
    MCParticleMap particleMap;

    for (MCParticlesToMCTruth::const_iterator iter = particleToTruthMap.begin(), iterEnd = particleToTruthMap.end(); iter != iterEnd; ++iter)
    {
        const art::Ptr<simb::MCParticle> particle = iter->first;
        particleMap[particle->TrackId()] = particle;
    }

    // Loop over MC truth objects
    int neutrinoCounter(0);

    lar_content::LArMCParticleFactory mcParticleFactory;

    for (MCTruthToMCParticles::const_iterator iter1 = truthToParticleMap.begin(), iterEnd1 = truthToParticleMap.end(); iter1 != iterEnd1; ++iter1)
    {
        const art::Ptr<simb::MCTruth> truth = iter1->first;

        if (truth->NeutrinoSet())
        {
            const simb::MCNeutrino neutrino(truth->GetNeutrino());
            ++neutrinoCounter;

            if (neutrinoCounter >= settings.m_uidOffset)
                throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraMCParticles --- detected an excessive number of MC neutrinos (" << neutrinoCounter << ")";

            const int neutrinoID(neutrinoCounter + 4 * settings.m_uidOffset);

            // Create Pandora 3D MC Particle
            lar_content::LArMCParticleParameters mcParticleParameters;
            mcParticleParameters.m_nuanceCode = neutrino.InteractionType();
            mcParticleParameters.m_energy = neutrino.Nu().E();
            mcParticleParameters.m_momentum = pandora::CartesianVector(neutrino.Nu().Px(), neutrino.Nu().Py(), neutrino.Nu().Pz());
            mcParticleParameters.m_vertex = pandora::CartesianVector(neutrino.Nu().Vx(), neutrino.Nu().Vy(), neutrino.Nu().Vz());
            mcParticleParameters.m_endpoint = pandora::CartesianVector(neutrino.Nu().Vx(), neutrino.Nu().Vy(), neutrino.Nu().Vz());
            mcParticleParameters.m_particleId = neutrino.Nu().PdgCode();
            mcParticleParameters.m_mcParticleType = pandora::MC_3D;
            mcParticleParameters.m_pParentAddress = (void*)((intptr_t)neutrinoID);

            for (const pandora::Pandora *const pPandora : pandoraInstanceList)
            {
                PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*pPandora, mcParticleParameters, mcParticleFactory));
            }

            // Loop over associated particles
            const MCParticleVector &particleVector = iter1->second;

            for (MCParticleVector::const_iterator iter2 = particleVector.begin(), iterEnd2 = particleVector.end(); iter2 != iterEnd2; ++iter2)
            {
                const art::Ptr<simb::MCParticle> particle = *iter2;
                const int trackID(particle->TrackId());

                // Mother/Daughter Links
                if (particle->Mother() == 0)
                {
                    for (const pandora::Pandora *const pPandora : pandoraInstanceList)
                    {
                        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::SetMCParentDaughterRelationship(*pPandora,
                            (void*)((intptr_t)neutrinoID), (void*)((intptr_t)trackID)));
                    }
                }
            }
        }
    }

    mf::LogDebug("LArPandora") << "   Number of Pandora neutrinos: " << neutrinoCounter << std::endl;

    // Loop over G4 particles
    int particleCounter(0);

    for (MCParticleMap::const_iterator iterI = particleMap.begin(), iterEndI = particleMap.end(); iterI != iterEndI; ++iterI)
    {
        const art::Ptr<simb::MCParticle> particle = iterI->second;

        if (particle->TrackId() != iterI->first)
            throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraMCParticles --- MC truth information appears to be scrambled in this event";

        if (particle->TrackId() >= settings.m_uidOffset)
            throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraMCParticles --- detected an excessive number of MC particles (" << particle->TrackId() << ")";

        ++particleCounter;

        for (const pandora::Pandora *const pPandora : pandoraInstanceList)
        {

            // Get the volume ID for this Pandora instance
            int volumeID(-1);

            try
            {
                volumeID = MultiPandoraApi::GetVolumeInfo(pPandora).GetIdNumber();
            }
            catch (pandora::StatusCodeException &)
            {
                continue;
            }

            // Find start and end trajectory points
            int firstT(-1), lastT(-1), nDrift(0);
            LArPandoraInput::GetTrueStartAndEndPoints(settings, driftVolumeMap, volumeID, particle, firstT, lastT, nDrift);

            if (firstT < 0 && lastT < 0)
            {
                firstT = 0; lastT = 0;
            }

            // Lookup position and kinematics at start and end points
            const float vtxX(particle->Vx(firstT));
            const float vtxY(particle->Vy(firstT));
            const float vtxZ(particle->Vz(firstT));

            const float endX(particle->Vx(lastT));
            const float endY(particle->Vy(lastT));
            const float endZ(particle->Vz(lastT));

            const float pX(particle->Px(firstT));
            const float pY(particle->Py(firstT));
            const float pZ(particle->Pz(firstT));
            const float E(particle->E(firstT));

            // Create 3D Pandora MC Particle
            lar_content::LArMCParticleParameters mcParticleParameters;
            mcParticleParameters.m_nuanceCode = 0;
            mcParticleParameters.m_energy = E;
            mcParticleParameters.m_particleId = particle->PdgCode();
            mcParticleParameters.m_momentum = pandora::CartesianVector(pX, pY, pZ);
            mcParticleParameters.m_vertex = pandora::CartesianVector(vtxX, vtxY, vtxZ);
            mcParticleParameters.m_endpoint = pandora::CartesianVector(endX, endY, endZ);
            mcParticleParameters.m_mcParticleType = pandora::MC_3D;
            mcParticleParameters.m_pParentAddress = (void*)((intptr_t)particle->TrackId());
            PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*pPandora, mcParticleParameters, mcParticleFactory));

            // Create Mother/Daughter Links between 3D MC Particles
            const int id_mother(particle->Mother());
            MCParticleMap::const_iterator iterJ = particleMap.find(id_mother);

            if (iterJ != particleMap.end())
                PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::SetMCParentDaughterRelationship(*pPandora,
                    (void*)((intptr_t)id_mother), (void*)((intptr_t)particle->TrackId())));
        }
    }

    mf::LogDebug("LArPandora") << "   Number of Pandora particles: " << particleCounter << std::endl;
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArPandoraInput::CreatePandoraMCParticles2D(const Settings &settings, const LArDriftVolumeMap &driftVolumeMap, const MCParticleVector &particleVector)
{
    mf::LogDebug("LArPandora") << " *** LArPandoraInput::CreatePandoraMCParticles2D(...) *** " << std::endl;

    if (!settings.m_pPrimaryPandora)
        throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraMCParticles2D --- primary Pandora instance does not exist ";

    PandoraInstanceList pandoraInstanceList(MultiPandoraApi::GetDaughterPandoraInstanceList(settings.m_pPrimaryPandora));

    if (pandoraInstanceList.empty())
        pandoraInstanceList.push_back(settings.m_pPrimaryPandora);

    lar_content::LArMCParticleFactory mcParticleFactory;

    for (MCParticleVector::const_iterator iter = particleVector.begin(), iterEnd = particleVector.end(); iter != iterEnd; ++iter)
    {
        const art::Ptr<simb::MCParticle> particle = *iter;

        if (particle->TrackId() >= settings.m_uidOffset)
            throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraMCParticles2D --- detected an excessive number of MC particles (" << particle->TrackId() << ")";

        // Loop over drift volumes
        for (const pandora::Pandora *const pPandora : pandoraInstanceList)
        {
            // Get the volume ID for this Pandora instance
            int volumeID(-1);

            try
            {
                volumeID = MultiPandoraApi::GetVolumeInfo(pPandora).GetIdNumber();
            }
            catch (pandora::StatusCodeException &)
            {
                continue;
            }

            // Find start and end trajectory points
            int firstT(-1), lastT(-1), nDrift(0);
            bool foundStartAndEndPoints(false);
            LArPandoraInput::GetTrueStartAndEndPoints(settings, driftVolumeMap, volumeID, particle, firstT, lastT, nDrift);

            if (firstT >= 0 && lastT >= 0)
            {
                foundStartAndEndPoints = true;
            }
            else
            {
                firstT = 0; lastT = 0;
            }

            if (!foundStartAndEndPoints)
                continue;

            // Lookup position and kinematics at start and end points
            const float vtxX(particle->Vx(firstT));
            const float vtxY(particle->Vy(firstT));
            const float vtxZ(particle->Vz(firstT));

            const float endX(particle->Vx(lastT));
            const float endY(particle->Vy(lastT));
            const float endZ(particle->Vz(lastT));

            const float pX(particle->Px(firstT));
            const float pY(particle->Py(firstT));
            const float pZ(particle->Pz(firstT));
            const float E(particle->E(firstT));

            // Create 2D Pandora MC Particles for Event Display
            const float dx(endX - vtxX);
            const float dy(endY - vtxY);
            const float dz(endZ - vtxZ);
            const float dw(lar_content::LArGeometryHelper::GetWireZPitch(*pPandora));

            if (dx * dx + dy * dy + dz * dz < 0.5 * dw * dw)
                continue;

            // Apply X0 corrections to 2D projections
            const float trueX0(LArPandoraInput::GetTrueX0(particle, firstT));

            // Create 2D Pandora MC Particles for each view (creating two MC Particles if there are two X0 corrections)
            lar_content::LArMCParticleParameters mcParticleParameters;
            mcParticleParameters.m_nuanceCode = 0;
            mcParticleParameters.m_energy = E;
            mcParticleParameters.m_particleId = particle->PdgCode();

            for (int n=0; n < nDrift; ++n)
            {
                const float correctX0((0==n) ? trueX0 : -1.f * trueX0);
                const int extraOffset((0==n) ? 0 : 3 * settings.m_uidOffset);

                // Create U projection
                mcParticleParameters.m_momentum = pandora::CartesianVector(pX, 0.f,
                    lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->PYPZtoPU(pY, pZ));
                mcParticleParameters.m_vertex = pandora::CartesianVector(vtxX + correctX0, 0.f,
                    lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoU(vtxY, vtxZ));
                mcParticleParameters.m_endpoint = pandora::CartesianVector(endX + correctX0,  0.f,
                    lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoU(endY, endZ));
                mcParticleParameters.m_mcParticleType = pandora::MC_VIEW_U;
                mcParticleParameters.m_pParentAddress = (void*)((intptr_t)(particle->TrackId() + 1 * settings.m_uidOffset + extraOffset));
                PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*pPandora, mcParticleParameters, mcParticleFactory));

                // Create V projection
                mcParticleParameters.m_momentum = pandora::CartesianVector(pX, 0.f,
                    lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->PYPZtoPV(pY, pZ));
                mcParticleParameters.m_vertex = pandora::CartesianVector(vtxX + correctX0, 0.f,
                    lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoV(vtxY, vtxZ));
                mcParticleParameters.m_endpoint = pandora::CartesianVector(endX + correctX0,  0.f,
                    lar_content::LArGeometryHelper::GetLArTransformationPlugin(*pPandora)->YZtoV(endY, endZ));
                mcParticleParameters.m_mcParticleType = pandora::MC_VIEW_V;
                mcParticleParameters.m_pParentAddress = (void*)((intptr_t)(particle->TrackId() + 2 * settings.m_uidOffset + extraOffset));
                PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*pPandora, mcParticleParameters, mcParticleFactory));

                // Create W projection
                mcParticleParameters.m_momentum = pandora::CartesianVector(pX, 0.f, pZ);
                mcParticleParameters.m_vertex = pandora::CartesianVector(vtxX + correctX0, 0.f, vtxZ);
                mcParticleParameters.m_endpoint = pandora::CartesianVector(endX + correctX0,  0.f, endZ);
                mcParticleParameters.m_mcParticleType = pandora::MC_VIEW_W;
                mcParticleParameters.m_pParentAddress = (void*)((intptr_t)(particle->TrackId() + 3 * settings.m_uidOffset + extraOffset));
                PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::MCParticle::Create(*pPandora, mcParticleParameters, mcParticleFactory));
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArPandoraInput::CreatePandoraMCLinks2D(const Settings &settings, const LArDriftVolumeMap &driftVolumeMap, const IdToHitMap &idToHitMap,
    const HitsToTrackIDEs &hitToParticleMap)
{
    mf::LogDebug("LArPandora") << " *** LArPandoraInput::CreatePandoraMCLinks(...) *** " << std::endl;

    if (!settings.m_pPrimaryPandora)
        throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraMCLinks2D --- primary Pandora instance does not exist ";

    for (IdToHitMap::const_iterator iterI = idToHitMap.begin(), iterEndI = idToHitMap.end(); iterI != iterEndI ; ++iterI)
    {
        const int hitID(iterI->first);
        const art::Ptr<recob::Hit> hit(iterI->second);
        const geo::WireID hit_WireID(hit->WireID());
        const pandora::Pandora *pPandora(nullptr);

        try
        {
            const int volumeID(LArPandoraGeometry::GetVolumeID(driftVolumeMap, hit_WireID.Cryostat, hit_WireID.TPC));
            pPandora = MultiPandoraApi::GetDaughterPandoraInstance(settings.m_pPrimaryPandora, volumeID);
        }
        catch (pandora::StatusCodeException &)
        {
        }

        if (!pPandora)
            continue;

        // Get list of associated MC particles
        HitsToTrackIDEs::const_iterator iterJ = hitToParticleMap.find(hit);

        if (hitToParticleMap.end() == iterJ)
            continue;

        const TrackIDEVector &trackCollection = iterJ->second;

        if (trackCollection.size() == 0)
            throw cet::exception("LArPandora") << " LArPandoraInput::CreatePandoraMCLinks2D --- found a hit without any associated MC truth information";

        // Create links between hits and MC particles
        for (unsigned int k = 0; k < trackCollection.size(); ++k)
        {
            const sim::TrackIDE trackIDE(trackCollection.at(k));
            const int trackID(std::abs(trackIDE.trackID)); // TODO: Find out why std::abs is needed
            const float energyFrac(trackIDE.energyFrac);

            PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::SetCaloHitToMCParticleRelationship(*pPandora,
                (void*)((intptr_t)hitID), (void*)((intptr_t)trackID), energyFrac));
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArPandoraInput::GetTrueStartAndEndPoints(const Settings &settings, const LArDriftVolumeMap &driftVolumeMap, const int volumeID,
    const art::Ptr<simb::MCParticle> &particle, int &firstT, int &lastT, int &nDrift)
{
    art::ServiceHandle<geo::Geometry> theGeometry;
    firstT = -1;  lastT  = -1;

    bool isNegX(false), isPosX(false);

    for (unsigned int icstat = 0; icstat < theGeometry->Ncryostats(); ++icstat)
    {
        for (unsigned int itpc = 0; itpc < theGeometry->NTPC(icstat); ++itpc)
        {
            if (LArPandoraGeometry::GetVolumeID(driftVolumeMap, icstat, itpc) != volumeID)
                continue;

            int thisfirstT(-1), thislastT(-1);
            LArPandoraInput::GetTrueStartAndEndPoints(icstat, itpc, particle, thisfirstT, thislastT);

            if (thisfirstT < 0)
                continue;

            if (firstT < 0 || thisfirstT < firstT)
                firstT = thisfirstT;

            if (lastT < 0 || thislastT > lastT)
                lastT = thislastT;

            const geo::TPCGeo &theTpc = theGeometry->Cryostat(icstat).TPC(itpc);
            isNegX = (isNegX || (theTpc.DriftDirection() == geo::kNegX));
            isPosX = (isPosX || (theTpc.DriftDirection() == geo::kPosX));
        }
    }

    nDrift = ((isNegX && isPosX) ? 2 : (isNegX || isPosX) ? 1 : 0);
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArPandoraInput::GetTrueStartAndEndPoints(const unsigned int cstat, const unsigned int tpc, const art::Ptr<simb::MCParticle> &particle,
    int &startT, int &endT)
{
    art::ServiceHandle<geo::Geometry> theGeometry;

    bool foundStartPosition(false);
    const int numTrajectoryPoints(static_cast<int>(particle->NumberTrajectoryPoints()));

    for (int nt = 0; nt < numTrajectoryPoints; ++nt)
    {
        const double pos[3] = {particle->Vx(nt), particle->Vy(nt), particle->Vz(nt)};
        geo::TPCID tpcID = theGeometry->FindTPCAtPosition(pos);

        if (!tpcID.isValid)
            continue;

        if (!(cstat == tpcID.Cryostat && tpc == tpcID.TPC))
            continue;

        endT = nt;

        if (!foundStartPosition)
        {
            startT = endT;
            foundStartPosition = true;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

float LArPandoraInput::GetTrueX0(const art::Ptr<simb::MCParticle> &particle, const int nt)
{
    art::ServiceHandle<geo::Geometry> theGeometry;
    auto const* theTime = lar::providerFrom<detinfo::DetectorClocksService>();
    auto const* theDetector = lar::providerFrom<detinfo::DetectorPropertiesService>();

    unsigned int which_tpc(0);
    unsigned int which_cstat(0);
    double pos[3] = {particle->Vx(nt), particle->Vy(nt), particle->Vz(nt)};
    theGeometry->PositionToTPC(pos, which_tpc, which_cstat);

    const float vtxT(particle->T(nt));
    const float vtxTDC(theTime->TPCG4Time2Tick(vtxT));
    const float vtxTDC0(theDetector->TriggerOffset());

    const geo::TPCGeo &theTpc = theGeometry->Cryostat(which_cstat).TPC(which_tpc);
    const float driftDir((theTpc.DriftDirection() == geo::kNegX) ? +1.0 :-1.0);
    return (driftDir * (vtxTDC - vtxTDC0) * theDetector->GetXTicksCoefficient());
}

//------------------------------------------------------------------------------------------------------------------------------------------

double LArPandoraInput::GetMips(const Settings &settings, const double hit_Charge, const geo::View_t hit_View)
{
    art::ServiceHandle<geo::Geometry> theGeometry;
    auto const* theDetector = lar::providerFrom<detinfo::DetectorPropertiesService>();

    // TODO: Check if this procedure is correct
    const double dQdX(hit_Charge / (theGeometry->WirePitch(hit_View))); // ADC/cm
    const double dQdX_e(dQdX / (theDetector->ElectronsToADC() * settings.m_recombination_factor)); // e/cm
    double dEdX(theDetector->BirksCorrection(dQdX_e));

    if ((dEdX < 0) || (dEdX > settings.m_dEdX_max))
        dEdX = settings.m_dEdX_max;

    const double mips(dEdX / settings.m_dEdX_mip);
    return mips;
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

LArPandoraInput::Settings::Settings() :
    m_pPrimaryPandora(nullptr),
    m_useHitWidths(true),
    m_uidOffset(100000000),
    m_dx_cm(0.5),
    m_int_cm(84.0),
    m_rad_cm(14.0),
    m_dEdX_max(25.0),
    m_dEdX_mip(2.0),
    m_mips_to_gev(3.5e-4),
    m_recombination_factor(0.63),
    m_globalViews(false),
    m_truncateReadout(false)
{
}

} // namespace lar_pandora
