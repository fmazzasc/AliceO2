// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file StrangenessTracker.h
/// \brief
///

#ifndef _ALICEO2_STRANGENESS_TRACKER_
#define _ALICEO2_STRANGENESS_TRACKER_

#include <gsl/gsl>
#include <TVector3.h>

#include "DataFormatsITSMFT/TopologyDictionary.h"
#include "StrangenessTracking/IndexTableUtils.h"
#include "StrangenessTracking/StrangenessTrackingConfigParam.h"
#include "ReconstructionDataFormats/PID.h"
#include "ReconstructionDataFormats/V0.h"
#include "ReconstructionDataFormats/Cascade.h"
#include "ReconstructionDataFormats/StrangeTrack.h"
#include "ReconstructionDataFormats/KinkTrack.h"
#include "ReconstructionDataFormats/VtxTrackIndex.h"
#include "ReconstructionDataFormats/VtxTrackRef.h"

#include "DataFormatsITS/TrackITS.h"
#include "ITSBase/GeometryTGeo.h"
#include "ReconstructionDataFormats/Track.h"
#include "DataFormatsITSMFT/CompCluster.h"
#include "DataFormatsGlobalTracking/RecoContainer.h"

#ifndef HomogeneousField
#define HomogeneousField
#endif

#include "DCAFitter/DCAFitterN.h"
#include "DetectorsBase/Propagator.h"
#include "KFParticle.h"
#include "KFVertex.h"
#include "KFPVertex.h"
#include "KFPTrack.h"

namespace o2
{
namespace strangeness_tracking
{

struct ClusAttachments {

  std::array<unsigned int, 7> arr;
};

class StrangenessTracker
{
 public:
  using StrangeTrack = o2::dataformats::StrangeTrack;
  using KinkTrack = o2::dataformats::KinkTrack;
  using PID = o2::track::PID;
  using TrackITS = o2::its::TrackITS;
  using ITSCluster = o2::BaseCluster<float>;
  using V0 = o2::dataformats::V0;
  using Cascade = o2::dataformats::Cascade;
  using GIndex = o2::dataformats::VtxTrackIndex;
  using DCAFitter2 = o2::vertexing::DCAFitterN<2>;
  using DCAFitter3 = o2::vertexing::DCAFitterN<3>;
  using MCLabContCl = o2::dataformats::MCTruthContainer<o2::MCCompLabel>;
  using MCLabSpan = gsl::span<const o2::MCCompLabel>;
  using VBracket = o2::math_utils::Bracket<int>;

  struct kinkTrackHelper {
    o2::track::TrackParCovF track;
    GIndex index;
    VBracket vtxBracket;
    GIndex itsRef;
  };

  StrangenessTracker() = default;
  ~StrangenessTracker() = default;

  bool loadData(const o2::globaltracking::RecoContainer& recoData);
  bool matchDecayToITStrack(float decayR);
  bool matchKinkToITSTrack(o2::track::TrackParCovF daughterTrack, bool KFParticle);
  void prepareITStracks();
  void process();
  bool updateTrack(const ITSCluster& clus, o2::track::TrackParCov& track);

  std::vector<ClusAttachments>& getClusAttachments() { return mClusAttachments; };
  std::vector<StrangeTrack>& getStrangeTrackVec() { return mStrangeTrackVec; };
  std::vector<KinkTrack>& getKinkTrackVec() { return mKinkTrackVec; };
  std::vector<o2::MCCompLabel>& getStrangeTrackLabels() { return mStrangeTrackLabels; };

  float getBz() const { return mBz; }
  void setBz(float d) { mBz = d; }
  void setClusterDictionary(const o2::itsmft::TopologyDictionary* d) { mDict = d; }
  void setCorrType(const o2::base::PropagatorImpl<float>::MatCorrType& type) { mCorrType = type; }
  void setConfigParams(const StrangenessTrackingParamConfig* params) { mStrParams = params; }
  void setMCTruthOn(bool v) { mMCTruthON = v; }

  void clear()
  {
    mDaughterTracks.clear();
    mClusAttachments.clear();
    mStrangeTrackVec.clear();
    mKinkTrackVec.clear();
    mTracksIdxTable.clear();
    mSortedITStracks.clear();
    mSortedITSindexes.clear();
    mITSvtxBrackets.clear();
    mInputITSclusters.clear();
    mInputClusterSizes.clear();
    if (mMCTruthON) {
      mStrangeTrackLabels.clear();
    }
  }

  void setupFitters()
  {
    mFitterV0.setBz(mBz);
    mFitter3Body.setBz(mBz);
    mFitterKink.setBz(mBz);
    mFitterV0.setUseAbsDCA(true);
    mFitter3Body.setUseAbsDCA(true);
    mFitterKink.setUseAbsDCA(true);
  }

  double calcV0alpha(const V0& v0)
  {
    std::array<float, 3> fV0mom, fPmom, fNmom = {0, 0, 0};
    v0.getProng(0).getPxPyPzGlo(fPmom);
    v0.getProng(1).getPxPyPzGlo(fNmom);
    v0.getPxPyPzGlo(fV0mom);

    TVector3 momNeg(fNmom[0], fNmom[1], fNmom[2]);
    TVector3 momPos(fPmom[0], fPmom[1], fPmom[2]);
    TVector3 momTot(fV0mom[0], fV0mom[1], fV0mom[2]);

    Double_t lQlNeg = momNeg.Dot(momTot) / momTot.Mag();
    Double_t lQlPos = momPos.Dot(momTot) / momTot.Mag();
    return (lQlPos - lQlNeg) / (lQlPos + lQlNeg);
  };

  double calcMotherMass(double p2Mother, double p2DauFirst, double p2DauSecond, PID pidDauFirst, PID pidDauSecond)
  {
    double m2DauFirst = PID::getMass2(pidDauFirst);
    double m2DauSecond = PID::getMass2(pidDauSecond);
    double motherE = sqrt(p2DauFirst + m2DauFirst) + sqrt(p2DauSecond + m2DauSecond); // E = sqrt(p^2 + m^2)
    double e2Mother = motherE * motherE;
    return std::sqrt(e2Mother - p2Mother);
  }

  double calcKinkMotherMass(std::array<float, 3UL> pMother, std::array<float, 3UL> pDaughter, PID pidDaughter, PID pidKink) // Kink = neuter Daughter
  {
    double m2kink = PID::getMass2(pidKink);
    double m2daughter = PID::getMass2(pidDaughter);
    double p2Mother = pMother[0] * pMother[0] + pMother[1] * pMother[1] + pMother[2] * pMother[2];
    double p2Daughter = pDaughter[0] * pDaughter[0] + pDaughter[1] * pDaughter[1] + pDaughter[2] * pDaughter[2];

    double eDaughter = sqrt(p2Daughter + m2daughter);
    std::array<float, 3> pKink = {pMother[0] - pDaughter[0], pMother[1] - pDaughter[1], pMother[2] - pDaughter[2]};
    double p2Kink = pKink[0] * pKink[0] + pKink[1] * pKink[1] + pKink[2] * pKink[2];
    double eKink = sqrt(m2kink + p2Kink);
    double eMother = eKink + eDaughter;

    return std::sqrt(eMother * eMother - p2Mother);
  }

  bool recreateV0(const o2::track::TrackParCov& posTrack, const o2::track::TrackParCov& negTrack, V0& newV0)
  {

    int nCand;
    try {
      nCand = mFitterV0.process(posTrack, negTrack);
    } catch (std::runtime_error& e) {
      return false;
    }
    if (!nCand || !mFitterV0.propagateTracksToVertex()) {
      return false;
    }

    const auto& v0XYZ = mFitterV0.getPCACandidatePos();

    auto& propPos = mFitterV0.getTrack(0, 0);
    auto& propNeg = mFitterV0.getTrack(1, 0);

    std::array<float, 3> pP, pN;
    propPos.getPxPyPzGlo(pP);
    propNeg.getPxPyPzGlo(pN);
    std::array<float, 3> pV0 = {pP[0] + pN[0], pP[1] + pN[1], pP[2] + pN[2]};
    newV0 = V0(v0XYZ, pV0, mFitterV0.calcPCACovMatrixFlat(0), propPos, propNeg, mV0dauIDs[0], mV0dauIDs[1], PID::HyperTriton);
    return true;
  };

  std::vector<ITSCluster> getTrackClusters()
  {
    std::vector<ITSCluster> outVec;
    auto firstClus = mITStrack.getFirstClusterEntry();
    auto ncl = mITStrack.getNumberOfClusters();
    for (int icl = 0; icl < ncl; icl++) {
      outVec.push_back(mInputITSclusters[mInputITSidxs[firstClus + icl]]);
    }
    return outVec;
  };

  std::vector<int> getTrackClusterSizes()
  {
    std::vector<int> outVec;
    auto firstClus = mITStrack.getFirstClusterEntry();
    auto ncl = mITStrack.getNumberOfClusters();
    for (int icl = 0; icl < ncl; icl++) {
      outVec.push_back(mInputClusterSizes[mInputITSidxs[firstClus + icl]]);
    }
    return outVec;
  };

  void getClusterSizes(std::vector<int>& clusSizeVec, const gsl::span<const o2::itsmft::CompClusterExt> ITSclus, gsl::span<const unsigned char>::iterator& pattIt, const o2::itsmft::TopologyDictionary* mdict)
  {
    for (unsigned int iClus{0}; iClus < ITSclus.size(); ++iClus) {
      auto& clus = ITSclus[iClus];
      auto pattID = clus.getPatternID();
      int npix;
      o2::itsmft::ClusterPattern patt;

      if (pattID == o2::itsmft::CompCluster::InvalidPatternID || mdict->isGroup(pattID)) {
        patt.acquirePattern(pattIt);
        npix = patt.getNPixels();
      } else {

        npix = mdict->getNpixels(pattID);
        patt = mdict->getPattern(pattID);
      }
      clusSizeVec[iClus] = npix;
    }
    // LOG(info) << " Patt Npixel: " << pattVec[0].getNPixels();
  }

  float getMatchingChi2(o2::track::TrackParCovF v0, const TrackITS ITStrack)
  {
    float alpha = ITStrack.getParamOut().getAlpha() , x = ITStrack.getParamOut().getX();
    if (v0.rotate(alpha)) {
      if (v0.propagateTo(x, mBz)) {
        return v0.getPredictedChi2(ITStrack.getParamOut());
      }
    }
    return -100;
  };

  o2::MCCompLabel getStrangeTrackLabel() // ITS label with fake flag recomputed
  {
    bool isFake = false;
    auto itsTrkLab = mITSTrkLabels[mStrangeTrack.mITSRef];
    for (unsigned int iLay = 0; iLay < 7; iLay++) {
      if (mITStrack.hasHitOnLayer(iLay) && mITStrack.isFakeOnLayer(iLay) && mStructClus.arr[iLay] == 0) {
        isFake = true;
        break;
      }
    }
    itsTrkLab.setFakeFlag(isFake);
    return itsTrkLab;
  }

 protected:
  bool mMCTruthON = false;                      /// flag availability of MC truth
  gsl::span<const TrackITS> mInputITStracks;    // input ITS tracks
  std::vector<VBracket> mITSvtxBrackets;        // time brackets for ITS tracks
  std::vector<int> mTracksIdxTable;             // index table for ITS tracks
  std::vector<int> mInputClusterSizes;          // input cluster sizes
  std::vector<ITSCluster> mInputITSclusters;    // input ITS clusters
  gsl::span<const int> mInputITSidxs;           // input ITS track-cluster indexes
  gsl::span<const V0> mInputV0tracks;           // input V0 of decay daughters
  gsl::span<const Cascade> mInputCascadeTracks; // input V0 of decay daughters
  const MCLabContCl* mITSClsLabels = nullptr;   /// input ITS Cluster MC labels
  MCLabSpan mITSTrkLabels;                      /// input ITS Track MC labels

  std::vector<o2::its::TrackITS> mSortedITStracks; // sorted ITS tracks
  std::vector<int> mSortedITSindexes;              // indexes of sorted ITS tracks
  IndexTableUtils mUtils;                          // structure for computing eta/phi matching selections

  std::vector<kinkTrackHelper> mKinkTracks; // kink tracks

  std::vector<StrangeTrack> mStrangeTrackVec;       // structure containing updated mother and daughter tracks
  std::vector<KinkTrack> mKinkTrackVec;             // structure containing updated mother and daughter kink tracks
  std::vector<ClusAttachments> mClusAttachments;    // # of attached tracks, -1 not attached, 0 for the mother, > 0 for the daughters
  std::vector<o2::MCCompLabel> mStrangeTrackLabels; // vector of MC labels for mother track

  const StrangenessTrackingParamConfig* mStrParams = nullptr;
  float mBz = -5; // Magnetic field
  const o2::itsmft::TopologyDictionary* mDict = nullptr;

  DCAFitter2 mFitterV0;    // optional DCA Fitter for recreating V0 with hypertriton mass hypothesis
  DCAFitter3 mFitter3Body; // optional DCA Fitter for final 3 Body refit
  DCAFitter2 mFitterKink;  // DCA Fitter for kink fit

  o2::base::PropagatorImpl<float>::MatCorrType mCorrType = o2::base::PropagatorImpl<float>::MatCorrType::USEMatCorrNONE; // use mat correction

  std::vector<o2::track::TrackParCovF> mDaughterTracks; // vector of daughter tracks
  StrangeTrack mStrangeTrack;                           // structure containing updated mother and daughter track refs
  KinkTrack mKinkTrack;                                 // structure containing updated mother and daughter kink refs
  ClusAttachments mStructClus;                          // # of attached tracks, 1 for mother, 2 for daughter
  o2::its::TrackITS mITStrack;                          // ITS track
  std::array<GIndex, 2> mV0dauIDs;                      // V0 daughter IDs

  ClassDefNV(StrangenessTracker, 1);
};

} // namespace strangeness_tracking
} // namespace o2

#endif //  _ALICEO2_STRANGENESS_TRACKER_
