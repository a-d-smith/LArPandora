/**
 *  @file  larpandora/LArPandoraInterface/LArPandoraHelper.h
 *
 *  @brief helper function for LArPandora producer modules
 *
 */
#ifndef LAR_PANDORA_HELPER_H
#define LAR_PANDORA_HELPER_H

#include "art/Persistency/Common/Ptr.h" 

#include "RecoBase/Track.h"
#include "RecoBase/Cluster.h"
#include "RecoBase/SpacePoint.h"
#include "RecoBase/Hit.h"
#include "RecoAlg/ClusterRecoUtil/ClusterParamsAlgBase.h"

namespace lar_pandora 
{

class LArPandoraHelper 
{
public:
    /**
     *  @brief Build a recob::Cluster object from an input vector of recob::Hit objects
     *
     *  @param id  The id code for the cluster
     *  @param hitVector  The input vector of hits
     *  @param algo Algorithm set to fill cluster members
     *  
     *  If you don't know which algorithm to pick, StandardClusterParamsAlg is
     *  a good default.
     */
    static recob::Cluster BuildCluster(
      const int id, const std::vector<art::Ptr<recob::Hit>> &hitVector,
      cluster::ClusterParamsAlgBase& algo
      );

    /**
     *  @brief Build a recob::Track object from an input vector of recob::SpacePoint objects
     *
     *  @param id  The id code for the track
     *  @param spacePointVector  The input vector of space points
     */
    static recob::Track BuildTrack(const int id, const std::vector<art::Ptr<recob::SpacePoint>> &spacePointVector);
};

} // namespace lar_pandora

#endif //  LAR_PANDORA_HELPER_H
