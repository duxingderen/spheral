//---------------------------------Spheral++----------------------------------//
// SVPHHydroBase -- The SVPH hydrodynamic package for Spheral++.
//
// Created by JMO, Sun Jul 28 20:57:01 PDT 2013
//----------------------------------------------------------------------------//
#include "SVPH/SVPHHydroBase.hh"
#include "SVPH/computeSVPHCorrections.hh"
#include "SVPH/SVPHCorrectionsPolicy.hh"
#include "SVPH/SVPHFieldNames.hh"
#include "SPH/computeSumVoronoiCellMassDensity.hh"
#include "NodeList/SmoothingScaleBase.hh"
#include "Hydro/HydroFieldNames.hh"
#include "Physics/GenericHydro.hh"
#include "DataBase/State.hh"
#include "DataBase/StateDerivatives.hh"
#include "DataBase/IncrementState.hh"
#include "DataBase/IncrementBoundedState.hh"
#include "DataBase/ReplaceState.hh"
#include "DataBase/ReplaceBoundedState.hh"
#include "Hydro/VolumePolicy.hh"
#include "Hydro/VoronoiMassDensityPolicy.hh"
#include "Hydro/SumVoronoiMassDensityPolicy.hh"
#include "Hydro/NonSymmetricSpecificThermalEnergyPolicy.hh"
#include "Hydro/PressurePolicy.hh"
#include "Hydro/SoundSpeedPolicy.hh"
#include "Hydro/PositionPolicy.hh"
#include "Mesh/MeshPolicy.hh"
#include "Mesh/generateMesh.hh"
#include "ArtificialViscosity/ArtificialViscosity.hh"
#include "DataBase/DataBase.hh"
#include "Field/FieldList.hh"
#include "Field/NodeIterators.hh"
#include "Boundary/Boundary.hh"
#include "Neighbor/ConnectivityMap.hh"
#include "Utilities/timingUtilities.hh"
#include "Utilities/safeInv.hh"
#include "Utilities/globalBoundingVolumes.hh"
#include "FileIO/FileIO.hh"
#include "Mesh/Mesh.hh"

#include <algorithm>
#include <fstream>
#include <map>
#include <vector>

namespace Spheral {


//------------------------------------------------------------------------------
// Construct with the given artificial viscosity and kernels.
//------------------------------------------------------------------------------
template<typename Dimension>
SVPHHydroBase<Dimension>::
SVPHHydroBase(const SmoothingScaleBase<Dimension>& smoothingScaleMethod,
              const TableKernel<Dimension>& W,
              ArtificialViscosity<Dimension>& Q,
              const double cfl,
              const bool useVelocityMagnitudeForDt,
              const bool compatibleEnergyEvolution,
              const bool XSVPH,
              const bool linearConsistent,
              const MassDensityType densityUpdate,
              const HEvolutionType HUpdate,
              const Scalar fcentroidal,
              const Vector& xmin,
              const Vector& xmax):
  GenericHydro<Dimension>(Q, cfl, useVelocityMagnitudeForDt),
  mKernel(W),
  mSmoothingScaleMethod(smoothingScaleMethod),
  mDensityUpdate(densityUpdate),
  mHEvolution(HUpdate),
  mCompatibleEnergyEvolution(compatibleEnergyEvolution),
  mXSVPH(XSVPH),
  mLinearConsistent(linearConsistent),
  mfcentroidal(fcentroidal),
  mXmin(xmin),
  mXmax(xmax),
  mMeshPtr(MeshPtr(new Mesh<Dimension>())),
  mA(FieldList<Dimension, Scalar>::Copy),
  mB(FieldList<Dimension, Vector>::Copy),
  mGradB(FieldList<Dimension, SymTensor>::Copy),
  mTimeStepMask(FieldList<Dimension, int>::Copy),
  mPressure(FieldList<Dimension, Scalar>::Copy),
  mSoundSpeed(FieldList<Dimension, Scalar>::Copy),
  mVolume(FieldList<Dimension, Scalar>::Copy),
  mSpecificThermalEnergy0(FieldList<Dimension, Scalar>::Copy),
  mHideal(FieldList<Dimension, SymTensor>::Copy),
  mMaxViscousPressure(FieldList<Dimension, Scalar>::Copy),
  mMassDensitySum(FieldList<Dimension, Scalar>::Copy),
  mWeightedNeighborSum(FieldList<Dimension, Scalar>::Copy),
  mMassSecondMoment(FieldList<Dimension, SymTensor>::Copy),
  mXSVPHDeltaV(FieldList<Dimension, Vector>::Copy),
  mDxDt(FieldList<Dimension, Vector>::Copy),
  mDvDt(FieldList<Dimension, Vector>::Copy),
  mDmassDensityDt(FieldList<Dimension, Scalar>::Copy),
  mDspecificThermalEnergyDt(FieldList<Dimension, Scalar>::Copy),
  mDHDt(FieldList<Dimension, SymTensor>::Copy),
  mDvDx(FieldList<Dimension, Tensor>::Copy),
  mInternalDvDx(FieldList<Dimension, Tensor>::Copy),
  mPairAccelerations(FieldList<Dimension, vector<Vector> >::Copy),
  mRestart(registerWithRestart(*this)) {
  // Delegate range checking to our assignment methods.
  this->fcentroidal(mfcentroidal);
}

//------------------------------------------------------------------------------
// Destructor
//------------------------------------------------------------------------------
template<typename Dimension>
SVPHHydroBase<Dimension>::
~SVPHHydroBase() {
}

//------------------------------------------------------------------------------
// On problem start up we need to initialize our internal data.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
initializeProblemStartup(DataBase<Dimension>& dataBase) {

  // Create storage for the pressure and sound speed.
  mPressure = dataBase.newFluidFieldList(0.0, HydroFieldNames::pressure);
  mSoundSpeed = dataBase.newFluidFieldList(0.0, HydroFieldNames::soundSpeed);

  // Initialize the pressure and sound speed.
  dataBase.fluidPressure(mPressure);
  dataBase.fluidSoundSpeed(mSoundSpeed);

  // Construct the mesh and volumes.
  NodeList<Dimension> voidNodes("internal void", 0, 0);
  vector<const NodeList<Dimension>*> nodeLists(dataBase.nodeListBegin(), dataBase.nodeListEnd());
  nodeLists.push_back(&voidNodes);
  // std::sort(nodeLists.begin(), nodeLists.end(), typename NodeListRegistrar<Dimension>::NodeListComparator());
  generateMesh<Dimension,
                          typename vector<const NodeList<Dimension>*>::iterator,
                          ConstBoundaryIterator>
    (nodeLists.begin(),
     nodeLists.end(),
     this->boundaryBegin(),
     this->boundaryEnd(),
     mXmin,
     mXmax,
     true,              // mesh ghost nodes
     false,             // generateVoid
     true,              // generateParallelConnectivity
     false,             // removeBoundaryZones
     2.0,               // voidThreshold
     *mMeshPtr,
     voidNodes);
  mVolume = dataBase.newFluidFieldList(0.0, HydroFieldNames::volume);
  for (unsigned nodeListi = 0; nodeListi != dataBase.numFluidNodeLists(); ++nodeListi) {
    const unsigned n = mVolume[nodeListi]->numInternalElements();
    const unsigned offset = mMeshPtr->offset(nodeListi);
    for (unsigned i = 0; i != n; ++i) {
      mVolume(nodeListi, i) = mMeshPtr->zone(offset + i).volume();
    }
  }

  // Compute the SVPH normalization and corrections.
  computeSVPHCorrections<Dimension>(dataBase.connectivityMap(),
                                    this->kernel(),
                                    mVolume, dataBase.fluidPosition(), dataBase.fluidHfield(),
                                    mA, mB, mGradB);

}

//------------------------------------------------------------------------------
// Register the state we need/are going to evolve.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
registerState(DataBase<Dimension>& dataBase,
              State<Dimension>& state) {

  typedef typename State<Dimension>::PolicyPointer PolicyPointer;

  // Create the local storage for time step mask, pressure, sound speed, and position weight.
  dataBase.resizeFluidFieldList(mTimeStepMask, 1, HydroFieldNames::timeStepMask);
  dataBase.resizeFluidFieldList(mA, 0.0, SVPHFieldNames::A_SVPH);
  dataBase.resizeFluidFieldList(mB, Vector::zero, SVPHFieldNames::B_SVPH);
  dataBase.resizeFluidFieldList(mGradB, Tensor::zero, SVPHFieldNames::gradB_SVPH);
  dataBase.fluidPressure(mPressure);
  dataBase.fluidSoundSpeed(mSoundSpeed);

  // If we're using the compatibile energy discretization, prepare to maintain a copy
  // of the thermal energy.
  dataBase.resizeFluidFieldList(mSpecificThermalEnergy0, 0.0);
  if (mCompatibleEnergyEvolution) {
    size_t nodeListi = 0;
    for (typename DataBase<Dimension>::FluidNodeListIterator itr = dataBase.fluidNodeListBegin();
         itr != dataBase.fluidNodeListEnd();
         ++itr, ++nodeListi) {
      *mSpecificThermalEnergy0[nodeListi] = (*itr)->specificThermalEnergy();
      (*mSpecificThermalEnergy0[nodeListi]).name(HydroFieldNames::specificThermalEnergy + "0");
    }
  }

  // Now register away.
  size_t nodeListi = 0;
  for (typename DataBase<Dimension>::FluidNodeListIterator itr = dataBase.fluidNodeListBegin();
       itr != dataBase.fluidNodeListEnd();
       ++itr, ++nodeListi) {

    // Mass.
    state.enroll((*itr)->mass());

    // Mass density.
    if (densityUpdate() == IntegrateDensity) {
      PolicyPointer rhoPolicy(new IncrementBoundedState<Dimension, Scalar>((*itr)->rhoMin(),
                                                                           (*itr)->rhoMax()));
      state.enroll((*itr)->massDensity(), rhoPolicy);
    } else {
      PolicyPointer rhoPolicy(new ReplaceBoundedState<Dimension, Scalar>((*itr)->rhoMin(),
                                                                         (*itr)->rhoMax()));
      state.enroll((*itr)->massDensity(), rhoPolicy);
    }

    // Mesh and volume.
    PolicyPointer meshPolicy(new MeshPolicy<Dimension>(*this, mXmin, mXmax));
    PolicyPointer volumePolicy(new VolumePolicy<Dimension>());
    state.enrollMesh(mMeshPtr);
    state.enroll(HydroFieldNames::mesh, meshPolicy);
    state.enroll(*mVolume[nodeListi], volumePolicy);

    // SVPH corrections.
    // All of these corrections are computed in the same method/policy, so we register
    // the A field with the update policy and the others just come along for the ride.
    PolicyPointer Apolicy(new SVPHCorrectionsPolicy<Dimension>(dataBase, this->kernel()));
    state.enroll(*mA[nodeListi], Apolicy);
    state.enroll(*mB[nodeListi]);
    state.enroll(*mGradB[nodeListi]);

    // Register the position update.
    // PolicyPointer positionPolicy(new PositionPolicy<Dimension>());
    PolicyPointer positionPolicy(new IncrementState<Dimension, Vector>());
    state.enroll((*itr)->positions(), positionPolicy);

    // Are we using the compatible energy evolution scheme?
    if (compatibleEnergyEvolution()) {
      PolicyPointer thermalEnergyPolicy(new NonSymmetricSpecificThermalEnergyPolicy<Dimension>(dataBase));
      PolicyPointer velocityPolicy(new IncrementState<Dimension, Vector>(HydroFieldNames::position,
                                                                         HydroFieldNames::specificThermalEnergy));
      state.enroll((*itr)->specificThermalEnergy(), thermalEnergyPolicy);
      state.enroll((*itr)->velocity(), velocityPolicy);
      state.enroll(*mSpecificThermalEnergy0[nodeListi]);
    } else {
      PolicyPointer thermalEnergyPolicy(new IncrementState<Dimension, Scalar>());
      PolicyPointer velocityPolicy(new IncrementState<Dimension, Vector>());
      state.enroll((*itr)->specificThermalEnergy(), thermalEnergyPolicy);
      state.enroll((*itr)->velocity(), velocityPolicy);
    }

    // Register the H tensor.
    const Scalar hmaxInv = 1.0/(*itr)->hmax();
    const Scalar hminInv = 1.0/(*itr)->hmin();
    if (HEvolution() == IntegrateH) {
      PolicyPointer Hpolicy(new IncrementBoundedState<Dimension, SymTensor, Scalar>(hmaxInv, hminInv));
      state.enroll((*itr)->Hfield(), Hpolicy);
    } else {
      CHECK(HEvolution() == IdealH);
      PolicyPointer Hpolicy(new ReplaceBoundedState<Dimension, SymTensor, Scalar>(hmaxInv, hminInv));
      state.enroll((*itr)->Hfield(), Hpolicy);
    }

    // Register the time step mask, initialized to 1 so that everything defaults to being
    // checked.
    state.enroll(*mTimeStepMask[nodeListi]);

    // Compute and register the pressure and sound speed.
    PolicyPointer pressurePolicy(new PressurePolicy<Dimension>());
    PolicyPointer csPolicy(new SoundSpeedPolicy<Dimension>());
    state.enroll(*mPressure[nodeListi], pressurePolicy);
    state.enroll(*mSoundSpeed[nodeListi], csPolicy);
  }
}

//------------------------------------------------------------------------------
// Register the state derivative fields.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
registerDerivatives(DataBase<Dimension>& dataBase,
                    StateDerivatives<Dimension>& derivs) {

  typedef typename StateDerivatives<Dimension>::KeyType Key;
  const string DxDtName = IncrementState<Dimension, Vector>::prefix() + HydroFieldNames::position;
  const string DvDtName = HydroFieldNames::hydroAcceleration;

  // Create the scratch fields.
  // Note we deliberately do not zero out the derivatives here!  This is because the previous step
  // info here may be used by other algorithms (like the CheapSynchronousRK2 integrator or
  // the ArtificialVisocisity::initialize step).
  dataBase.resizeFluidFieldList(mHideal, SymTensor::zero, ReplaceBoundedState<Dimension, Field<Dimension, SymTensor> >::prefix() + HydroFieldNames::H, false);
  dataBase.resizeFluidFieldList(mMaxViscousPressure, 0.0, HydroFieldNames::maxViscousPressure, false);
  dataBase.resizeFluidFieldList(mMassDensitySum, 0.0, ReplaceState<Dimension, Field<Dimension, SymTensor> >::prefix() + HydroFieldNames::massDensity, false);
  dataBase.resizeFluidFieldList(mWeightedNeighborSum, 0.0, HydroFieldNames::weightedNeighborSum, false);
  dataBase.resizeFluidFieldList(mMassSecondMoment, SymTensor::zero, HydroFieldNames::massSecondMoment, false);
  dataBase.resizeFluidFieldList(mXSVPHDeltaV, Vector::zero, HydroFieldNames::XSPHDeltaV, false);
  dataBase.resizeFluidFieldList(mDxDt, Vector::zero, IncrementState<Dimension, Field<Dimension, Vector> >::prefix() + HydroFieldNames::position, false);
  dataBase.resizeFluidFieldList(mDvDt, Vector::zero, HydroFieldNames::hydroAcceleration, false);
  dataBase.resizeFluidFieldList(mDmassDensityDt, 0.0, IncrementState<Dimension, Field<Dimension, Scalar> >::prefix() + HydroFieldNames::massDensity, false);
  dataBase.resizeFluidFieldList(mDspecificThermalEnergyDt, 0.0, IncrementState<Dimension, Field<Dimension, Scalar> >::prefix() + HydroFieldNames::specificThermalEnergy, false);
  dataBase.resizeFluidFieldList(mDHDt, SymTensor::zero, IncrementState<Dimension, Field<Dimension, Vector> >::prefix() + HydroFieldNames::H, false);
  dataBase.resizeFluidFieldList(mDvDx, Tensor::zero, HydroFieldNames::velocityGradient, false);
  dataBase.resizeFluidFieldList(mInternalDvDx, Tensor::zero, HydroFieldNames::internalVelocityGradient, false);
  dataBase.resizeFluidFieldList(mPairAccelerations, vector<Vector>(), HydroFieldNames::pairAccelerations, false);

  size_t i = 0;
  for (typename DataBase<Dimension>::FluidNodeListIterator itr = dataBase.fluidNodeListBegin();
       itr != dataBase.fluidNodeListEnd();
       ++itr, ++i) {
    derivs.enroll(*mHideal[i]);
    derivs.enroll(*mMaxViscousPressure[i]);
    derivs.enroll(*mMassDensitySum[i]);
    derivs.enroll(*mWeightedNeighborSum[i]);
    derivs.enroll(*mMassSecondMoment[i]);
    derivs.enroll(*mXSVPHDeltaV[i]);

    // These two (the position and velocity updates) may be registered
    // by other physics packages as well, so we need to be careful
    // not to duplicate if so.
    const Key DxDtKey = State<Dimension>::buildFieldKey(DxDtName, (*itr)->name());
    const Key DvDtKey = State<Dimension>::buildFieldKey(DvDtName, (*itr)->name());
    if (not derivs.registered(DxDtKey)) derivs.enroll(*mDxDt[i]);
    if (not derivs.registered(DvDtKey)) derivs.enroll(*mDvDt[i]);

    derivs.enroll(*mDmassDensityDt[i]);
    derivs.enroll(*mDspecificThermalEnergyDt[i]);
    derivs.enroll(*mDHDt[i]);
    derivs.enroll(*mDvDx[i]);
    derivs.enroll(*mInternalDvDx[i]);
    derivs.enroll(*mPairAccelerations[i]);
  }
}

//------------------------------------------------------------------------------
// Initialize the hydro before trying to evaluateDerivatives.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
initialize(const typename Dimension::Scalar time,
           const typename Dimension::Scalar dt,
           const DataBase<Dimension>& dataBase,
           State<Dimension>& state,
           StateDerivatives<Dimension>& derivs) {

  // Get the artificial viscosity and initialize it.
  // We depend on the caller knowing to finalize the ghost boundaries for the Q.
  ArtificialViscosity<Dimension>& Q = this->artificialViscosity();
  Q.initialize(dataBase, 
               state,
               derivs,
               this->boundaryBegin(),
               this->boundaryEnd(),
               time, 
               dt,
               this->kernel());
}

//------------------------------------------------------------------------------
// Determine the principle derivatives.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
evaluateDerivatives(const typename Dimension::Scalar time,
                    const typename Dimension::Scalar dt,
                    const DataBase<Dimension>& dataBase,
                    const State<Dimension>& state,
                    StateDerivatives<Dimension>& derivatives) const {

  // Get the ArtificialViscosity.
  ArtificialViscosity<Dimension>& Q = this->artificialViscosity();

  // The kernels and such.
  const TableKernel<Dimension>& W = this->kernel();

  // A few useful constants we'll use in the following loop.
  typedef typename Timing::Time Time;
  const Scalar W0 = W(0.0, 1.0);

  // The connectivity.
  const ConnectivityMap<Dimension>& connectivityMap = dataBase.connectivityMap();
  const vector<const NodeList<Dimension>*>& nodeLists = connectivityMap.nodeLists();
  const size_t numNodeLists = nodeLists.size();

  // Get the state and derivative FieldLists.
  // State FieldLists.
  const FieldList<Dimension, Scalar> mass = state.fields(HydroFieldNames::mass, 0.0);
  const FieldList<Dimension, Vector> position = state.fields(HydroFieldNames::position, Vector::zero);
  const FieldList<Dimension, Vector> velocity = state.fields(HydroFieldNames::velocity, Vector::zero);
  const FieldList<Dimension, Scalar> massDensity = state.fields(HydroFieldNames::massDensity, 0.0);
  const FieldList<Dimension, Scalar> specificThermalEnergy = state.fields(HydroFieldNames::specificThermalEnergy, 0.0);
  const FieldList<Dimension, SymTensor> H = state.fields(HydroFieldNames::H, SymTensor::zero);
  const FieldList<Dimension, Scalar> pressure = state.fields(HydroFieldNames::pressure, 0.0);
  const FieldList<Dimension, Scalar> soundSpeed = state.fields(HydroFieldNames::soundSpeed, 0.0);
  const FieldList<Dimension, Scalar> volume = state.fields(HydroFieldNames::volume, 0.0);
  const FieldList<Dimension, Scalar> A = state.fields(SVPHFieldNames::A_SVPH, 0.0);
  CHECK(mass.size() == numNodeLists);
  CHECK(position.size() == numNodeLists);
  CHECK(velocity.size() == numNodeLists);
  CHECK(massDensity.size() == numNodeLists);
  CHECK(specificThermalEnergy.size() == numNodeLists);
  CHECK(H.size() == numNodeLists);
  CHECK(pressure.size() == numNodeLists);
  CHECK(soundSpeed.size() == numNodeLists);
  CHECK(volume.size() == numNodeLists);
  CHECK(A.size() == numNodeLists);

  // Derivative FieldLists.
  FieldList<Dimension, Scalar> rhoSum = derivatives.fields(ReplaceState<Dimension, Field<Dimension, Scalar> >::prefix() + HydroFieldNames::massDensity, 0.0);
  FieldList<Dimension, Vector> DxDt = derivatives.fields(IncrementState<Dimension, Field<Dimension, Vector> >::prefix() + HydroFieldNames::position, Vector::zero);
  FieldList<Dimension, Scalar> DrhoDt = derivatives.fields(IncrementState<Dimension, Field<Dimension, Scalar> >::prefix() + HydroFieldNames::massDensity, 0.0);
  FieldList<Dimension, Vector> DvDt = derivatives.fields(HydroFieldNames::hydroAcceleration, Vector::zero);
  FieldList<Dimension, Scalar> DepsDt = derivatives.fields(IncrementState<Dimension, Field<Dimension, Scalar> >::prefix() + HydroFieldNames::specificThermalEnergy, 0.0);
  FieldList<Dimension, Tensor> DvDx = derivatives.fields(HydroFieldNames::velocityGradient, Tensor::zero);
  FieldList<Dimension, Tensor> localDvDx = derivatives.fields(HydroFieldNames::internalVelocityGradient, Tensor::zero);
  FieldList<Dimension, SymTensor> DHDt = derivatives.fields(IncrementState<Dimension, Field<Dimension, SymTensor> >::prefix() + HydroFieldNames::H, SymTensor::zero);
  FieldList<Dimension, SymTensor> Hideal = derivatives.fields(ReplaceBoundedState<Dimension, Field<Dimension, SymTensor> >::prefix() + HydroFieldNames::H, SymTensor::zero);
  FieldList<Dimension, Scalar> maxViscousPressure = derivatives.fields(HydroFieldNames::maxViscousPressure, 0.0);
  FieldList<Dimension, vector<Vector> > pairAccelerations = derivatives.fields(HydroFieldNames::pairAccelerations, vector<Vector>());
  FieldList<Dimension, Vector> XSVPHDeltaV = derivatives.fields(HydroFieldNames::XSPHDeltaV, Vector::zero);
  FieldList<Dimension, Scalar> weightedNeighborSum = derivatives.fields(HydroFieldNames::weightedNeighborSum, 0.0);
  FieldList<Dimension, SymTensor> massSecondMoment = derivatives.fields(HydroFieldNames::massSecondMoment, SymTensor::zero);
  CHECK(rhoSum.size() == numNodeLists);
  CHECK(DxDt.size() == numNodeLists);
  CHECK(DrhoDt.size() == numNodeLists);
  CHECK(DvDt.size() == numNodeLists);
  CHECK(DepsDt.size() == numNodeLists);
  CHECK(DvDx.size() == numNodeLists);
  CHECK(localDvDx.size() == numNodeLists);
  CHECK(DHDt.size() == numNodeLists);
  CHECK(Hideal.size() == numNodeLists);
  CHECK(maxViscousPressure.size() == numNodeLists);
  CHECK(pairAccelerations.size() == numNodeLists);
  CHECK(XSVPHDeltaV.size() == numNodeLists);
  CHECK(weightedNeighborSum.size() == numNodeLists);
  CHECK(massSecondMoment.size() == numNodeLists);

  // Size up the pair-wise accelerations before we start.
  if (mCompatibleEnergyEvolution) {
    size_t nodeListi = 0;
    for (typename DataBase<Dimension>::ConstFluidNodeListIterator itr = dataBase.fluidNodeListBegin();
         itr != dataBase.fluidNodeListEnd();
         ++itr, ++nodeListi) {
      for (int i = 0; i != (*itr)->numInternalNodes(); ++i) {
        pairAccelerations(nodeListi, i).reserve(connectivityMap.numNeighborsForNode(*itr, i));
      }
    }
  }

  // Start our big loop over all FluidNodeLists.
  size_t nodeListi = 0;
  for (typename DataBase<Dimension>::ConstFluidNodeListIterator itr = dataBase.fluidNodeListBegin();
       itr != dataBase.fluidNodeListEnd();
       ++itr, ++nodeListi) {
    const NodeList<Dimension>& nodeList = **itr;
    const int firstGhostNodei = nodeList.firstGhostNode();
    const Scalar hmin = nodeList.hmin();
    const Scalar hmax = nodeList.hmax();
    const Scalar hminratio = nodeList.hminratio();
    const int maxNumNeighbors = nodeList.maxNumNeighbors();
    const Scalar nPerh = nodeList.nodesPerSmoothingScale();

    // Get the work field for this NodeList.
    Field<Dimension, Scalar>& workFieldi = nodeList.work();

    // Iterate over the internal nodes in this NodeList.
    for (typename ConnectivityMap<Dimension>::const_iterator iItr = connectivityMap.begin(nodeListi);
         iItr != connectivityMap.end(nodeListi);
         ++iItr) {
      const int i = *iItr;

      // Prepare to accumulate the time.
      const Time start = Timing::currentTime();
      size_t ncalc = 0;

      // Get the state for node i.
      const Vector& ri = position(nodeListi, i);
      const Scalar& mi = mass(nodeListi, i);
      const Vector& vi = velocity(nodeListi, i);
      const Scalar& rhoi = massDensity(nodeListi, i);
      const Scalar& epsi = specificThermalEnergy(nodeListi, i);
      const Scalar& Pi = pressure(nodeListi, i);
      const SymTensor& Hi = H(nodeListi, i);
      const Scalar& ci = soundSpeed(nodeListi, i);
      const Scalar& Vi = volume(nodeListi, i);
      const Scalar& Ai = A(nodeListi, i);
      const Scalar Hdeti = Hi.Determinant();
      CHECK(mi > 0.0);
      CHECK(rhoi > 0.0);
      CHECK(Vi > 0.0);
      CHECK(Ai > 0.0);

      Scalar& rhoSumi = rhoSum(nodeListi, i);
      Vector& DxDti = DxDt(nodeListi, i);
      Scalar& DrhoDti = DrhoDt(nodeListi, i);
      Vector& DvDti = DvDt(nodeListi, i);
      Scalar& DepsDti = DepsDt(nodeListi, i);
      Tensor& DvDxi = DvDx(nodeListi, i);
      Tensor& localDvDxi = localDvDx(nodeListi, i);
      SymTensor& DHDti = DHDt(nodeListi, i);
      SymTensor& Hideali = Hideal(nodeListi, i);
      Scalar& maxViscousPressurei = maxViscousPressure(nodeListi, i);
      vector<Vector>& pairAccelerationsi = pairAccelerations(nodeListi, i);
      Vector& XSVPHDeltaVi = XSVPHDeltaV(nodeListi, i);
      Scalar& weightedNeighborSumi = weightedNeighborSum(nodeListi, i);
      SymTensor& massSecondMomenti = massSecondMoment(nodeListi, i);
      Scalar& worki = workFieldi(i);

      // Get the connectivity info for this node.
      const vector< vector<int> >& fullConnectivity = connectivityMap.connectivityForNode(&nodeList, i);

      // Iterate over the NodeLists.
      for (size_t nodeListj = 0; nodeListj != numNodeLists; ++nodeListj) {

        // Connectivity of this node with this NodeList.  We only need to proceed if
        // there are some nodes in this list.
        const vector<int>& connectivity = fullConnectivity[nodeListj];
        if (connectivity.size() > 0) {
          const double fweightij = 1.0; // (nodeListi == nodeListj ? 1.0 : 0.2);
          const int firstGhostNodej = nodeLists[nodeListj]->firstGhostNode();

          // Loop over the neighbors.
#if defined __INTEL_COMPILER
#pragma vector always
#endif
          for (vector<int>::const_iterator jItr = connectivity.begin();
               jItr != connectivity.end();
               ++jItr) {
            const int j = *jItr;

            // Only proceed if this node pair has not been calculated yet.
            if (connectivityMap.calculatePairInteraction(nodeListi, i, 
                                                         nodeListj, j,
                                                         firstGhostNodej)) {
              ++ncalc;

              // Get the state for node j
              const Vector& rj = position(nodeListj, j);
              const Scalar& mj = mass(nodeListj, j);
              const Vector& vj = velocity(nodeListj, j);
              const Scalar& rhoj = massDensity(nodeListj, j);
              const Scalar& epsj = specificThermalEnergy(nodeListj, j);
              const Scalar& Pj = pressure(nodeListj, j);
              const SymTensor& Hj = H(nodeListj, j);
              const Scalar& cj = soundSpeed(nodeListj, j);
              const Scalar& Vj = volume(nodeListj, j);
              const Scalar& Aj = A(nodeListj, j);
              const Scalar Hdetj = Hj.Determinant();
              CHECK(mj > 0.0);
              CHECK(rhoj > 0.0);
              CHECK(Vj > 0.0);
              CHECK(Aj > 0.0);

              Scalar& rhoSumj = rhoSum(nodeListj, j);
              Vector& DxDtj = DxDt(nodeListj, j);
              Scalar& DrhoDtj = DrhoDt(nodeListj, j);
              Vector& DvDtj = DvDt(nodeListj, j);
              Scalar& DepsDtj = DepsDt(nodeListj, j);
              Tensor& DvDxj = DvDx(nodeListj, j);
              Tensor& localDvDxj = localDvDx(nodeListj, j);
              Scalar& maxViscousPressurej = maxViscousPressure(nodeListj, j);
              vector<Vector>& pairAccelerationsj = pairAccelerations(nodeListj, j);
              Vector& XSVPHDeltaVj = XSVPHDeltaV(nodeListj, j);
              Scalar& weightedNeighborSumj = weightedNeighborSum(nodeListj, j);
              SymTensor& massSecondMomentj = massSecondMoment(nodeListj, j);

              // Node displacement.
              const Vector rij = ri - rj;
              const Vector etai = Hi*rij;
              const Vector etaj = Hj*rij;
              const Scalar etaMagi = etai.magnitude();
              const Scalar etaMagj = etaj.magnitude();
              CHECK(etaMagi >= 0.0);
              CHECK(etaMagj >= 0.0);

              // Symmetrized kernel weight and gradient.
              const Vector Hetai = Hi*etai.unitVector();
              Scalar Wi, gWi;
              const std::pair<double, double> WWi = W.kernelAndGradValue(etaMagi, Hdeti, Wi, gWi);
              const Vector gradWi = gWi*Hetai;

              const Vector Hetaj = Hj*etaj.unitVector();
              Scalar Wj, gWj;
              W.kernelAndGradValue(etaMagj, Hdetj, Wj, gWj);
              const Vector gradWj = gWj*Hetaj;

              // Zero'th and second moment of the node distribution -- used for the
              // ideal H calculation.
              const double rij2 = rij.magnitude2();
              const SymTensor thpt = rij.selfdyad()/(rij2 + 1.0e-10) / FastMath::square(Dimension::pownu12(rij2 + 1.0e-10));
              weightedNeighborSumi += fweightij*std::abs(gWi);
              weightedNeighborSumj += fweightij*std::abs(gWj);
              massSecondMomenti += fweightij*gradWi.magnitude2()*thpt;
              massSecondMomentj += fweightij*gradWj.magnitude2()*thpt;

              // Contribution to the sum density (only if the same material).
              if (nodeListi == nodeListj) {
                rhoSumi += mj*Wi;
                rhoSumj += mi*Wj;
              }

              // Mass density evolution.
              const Vector vij = vi - vj;
              DrhoDti += Vj*rhoi*vij.dot(gradWj);
              DrhoDtj += Vi*rhoj*vij.dot(gradWi);

              // // Compute the pair-wise artificial viscosity.
              // const pair<Tensor, Tensor> QPiij = Q.Piij(nodeListi, i, nodeListj, j,
              //                                           ri, etai, vi, rhoi, ci, Hi,
              //                                           rj, etaj, vj, rhoj, cj, Hj);
              // const Vector Qacci = 0.5*mj*(QPiij.first *gradWi);
              // const Vector Qaccj = 0.5*mi*(QPiij.second*gradWj);
              // const Scalar workQi = vij.dot(Qacci);
              // const Scalar workQj = vij.dot(Qaccj);
              // const Scalar Qi = rhoi*rhoi*(QPiij.first. diagonalElements().maxAbsElement());
              // const Scalar Qj = rhoj*rhoj*(QPiij.second.diagonalElements().maxAbsElement());
              // maxViscousPressurei = max(maxViscousPressurei, Qi);
              // maxViscousPressurej = max(maxViscousPressurej, Qj);

              // Compute the pair-wise artificial viscosity.
              const pair<Tensor, Tensor> QPiij = Q.Piij(nodeListi, i, nodeListj, j,
                                                        ri, etai, vi, rhoi, ci, Hi,
                                                        rj, etaj, vj, rhoj, cj, Hj);
              const Vector Qacci = Ai*Vj*(rhoi*rhoi*QPiij.first - rhoj*rhoj*QPiij.second)/rhoi * gradWj;
              const Vector Qaccj = Aj*Vi*(rhoi*rhoi*QPiij.first - rhoj*rhoj*QPiij.second)/rhoj * gradWi;
              // const Vector Qacci = -rhoj*QPiij.second*Ai*Vj * gradWj;
              // const Vector Qaccj =  rhoi*QPiij.first *Aj*Vi * gradWi;
              const Scalar workQi = Ai*Vj*rhoi*QPiij.first.xx() *vij.dot(gradWj);
              const Scalar workQj = Aj*Vi*rhoj*QPiij.second.xx()*vij.dot(gradWi);
              // const Scalar workQi = -mi/(mi + mj)*(vi.dot(Qacci) + vj.dot(Qaccj));
              // const Scalar workQj = -mj/(mi + mj)*(vi.dot(Qacci) + vj.dot(Qaccj));
              const Scalar Qi = rhoi*rhoi*(QPiij.first. diagonalElements().maxAbsElement());
              const Scalar Qj = rhoj*rhoj*(QPiij.second.diagonalElements().maxAbsElement());
              maxViscousPressurei = max(maxViscousPressurei, Qi);
              maxViscousPressurej = max(maxViscousPressurej, Qj);

              // Acceleration.
              CHECK(rhoi > 0.0);
              CHECK(rhoj > 0.0);
              const Vector deltaDvDti = Ai*Vj*(Pi - Pj)/rhoi*gradWj + Qacci; // - Qacci - Qaccj;
              const Vector deltaDvDtj = Aj*Vi*(Pi - Pj)/rhoj*gradWi + Qaccj; // + Qacci + Qaccj;
              // const Vector aij = (Pi - Pj)*Ai*Vj/rhoi * gradWj + Qacci;
              // const Vector aji = (Pi - Pj)*Aj*Vi/rhoj * gradWi + Qaccj;
              // const Vector Fc = mi*aij + mj*aji;
              // const Vector da = Fc/(mi + mj);
              // const Vector deltaDvDti = aij - da;
              // const Vector deltaDvDtj = aji - da;
              // CHECK2(fuzzyEqual(Fc.dot(mi*deltaDvDti + mj*deltaDvDtj), Fc.magnitude2(), 1.0e-10),
              //        "Pair-wise forces should sum to same central force:  "
              //        << Fc << " "
              //        << (mi*deltaDvDti + mj*deltaDvDtj));
              // CHECK2(fuzzyEqual(-mi*mj*deltaDvDti.dot(deltaDvDtj), mi*mi*deltaDvDti.magnitude2(), 1.0e-10),
              //        "Pair-wise forces should be equal and opposite:  "
              //        << mi*deltaDvDti << " "
              //        << mj*deltaDvDtj);
              DvDti += deltaDvDti;
              DvDtj += deltaDvDtj;

              // Specific thermal energy evolution.
              DepsDti += Ai*Vj*Pi/rhoi*vij.dot(gradWj) + workQi;
              DepsDtj += Aj*Vi*Pj/rhoj*vij.dot(gradWi) + workQj;
              if (mCompatibleEnergyEvolution) {
                pairAccelerationsi.push_back(deltaDvDti);
                pairAccelerationsj.push_back(deltaDvDtj);
              }

              // Velocity gradient.
              const Tensor deltaDvDxi = Vj*vij.dyad(gradWi);
              const Tensor deltaDvDxj = Vi*vij.dyad(gradWj);
              DvDxi -= deltaDvDxi;
              DvDxj -= deltaDvDxj;
              if (nodeListi == nodeListj) {
                localDvDxi -= deltaDvDxi;
                localDvDxj -= deltaDvDxj;
              }

              // Estimate of delta v (for XSVPH).
              if (mXSVPH and (nodeListi == nodeListj)) {
                const double fXSVPH = max(0.0, min(1.0, abs(vij.dot(rij)*safeInv(vij.magnitude()*rij.magnitude()))));
                CHECK(fXSVPH >= 0.0 and fXSVPH <= 1.0);
                XSVPHDeltaVi -= fXSVPH*Vj*Wj*vij;
                XSVPHDeltaVj += fXSVPH*Vi*Wj*vij;
              }

            }
          }
        }
      }
      const size_t numNeighborsi = connectivityMap.numNeighborsForNode(&nodeList, i);
      CHECK2(not mCompatibleEnergyEvolution or 
             (i >= firstGhostNodei and pairAccelerationsi.size() == 0) or
             (pairAccelerationsi.size() == numNeighborsi),
             "Bad sizing for pair accelerations!  "
             << i << " "
             << firstGhostNodei << " "
             << pairAccelerationsi.size() << " "
             << numNeighborsi);

      // Get the time for pairwise interactions.
      const Scalar deltaTimePair = Timing::difference(start, Timing::currentTime())/(ncalc + 1.0e-30);

      // Finish the density sum.
      rhoSumi += mi*W0*Hdeti;
      rhoSumi *= Ai;

      // Finish the continuity equation.
      DrhoDti *= Ai;

      // Finish the gradient of the velocity.
      DvDxi *= Ai;
      localDvDxi *= Ai;

      // Complete the moments of the node distribution for use in the ideal H calculation.
      weightedNeighborSumi = Dimension::rootnu(max(0.0, weightedNeighborSumi/Hdeti));
      massSecondMomenti /= Hdeti*Hdeti;
      // weightedNeighborSumi = Dimension::rootnu(max(0.0, weightedNeighborSumi));

      // Determine the position evolution, based on whether we're doing XSVPH or not.
      if (mXSVPH) {
        DxDti = vi + Ai*XSVPHDeltaVi;
      } else {
        DxDti = vi;
      }

      // // Apply any centroidal filtering.
      // DxDti = (1.0 - mfcentroidal)*DxDti + mfcentroidal*(zonei.position() - ri)/dt;

      // The H tensor evolution.
      DHDti = mSmoothingScaleMethod.smoothingScaleDerivative(Hi,
                                                             ri,
                                                             DvDxi,
                                                             hmin,
                                                             hmax,
                                                             hminratio,
                                                             nPerh);
      Hideali = mSmoothingScaleMethod.newSmoothingScale(Hi,
                                                        ri,
                                                        weightedNeighborSumi,
                                                        massSecondMomenti,
                                                        W,
                                                        hmin,
                                                        hmax,
                                                        hminratio,
                                                        nPerh,
                                                        connectivityMap,
                                                        nodeListi,
                                                        i);

      // Increment the work for i.
      worki += Timing::difference(start, Timing::currentTime());

      // Now add the pairwise time for each neighbor we computed here.
      for (int nodeListj = 0; nodeListj != numNodeLists; ++nodeListj) {
        const vector<int>& connectivity = fullConnectivity[nodeListj];
        if (connectivity.size() > 0) {
          const int firstGhostNodej = nodeLists[nodeListj]->firstGhostNode();
          Field<Dimension, Scalar>& workFieldj = nodeLists[nodeListj]->work();
#if defined __INTEL_COMPILER
#pragma vector always
#endif
          for (vector<int>::const_iterator jItr = connectivity.begin();
               jItr != connectivity.end();
               ++jItr) {
            const int j = *jItr;
            if (connectivityMap.calculatePairInteraction(nodeListi, i, 
                                                         nodeListj, j,
                                                         firstGhostNodej)) {
              workFieldj(j) += deltaTimePair;
            }
          }
        }
      }
    }
  }
}

//------------------------------------------------------------------------------
// Finalize the derivatives.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
finalizeDerivatives(const typename Dimension::Scalar time,
                    const typename Dimension::Scalar dt,
                    const DataBase<Dimension>& dataBase,
                    const State<Dimension>& state,
                    StateDerivatives<Dimension>& derivs) const {

  // If we're using the compatible energy discretization, we need to enforce
  // boundary conditions on the accelerations.
  if (compatibleEnergyEvolution()) {
    FieldList<Dimension, Vector> accelerations = derivs.fields(HydroFieldNames::hydroAcceleration, Vector::zero);
    for (ConstBoundaryIterator boundaryItr = this->boundaryBegin();
         boundaryItr != this->boundaryEnd();
         ++boundaryItr) (*boundaryItr)->applyFieldListGhostBoundary(accelerations);
    for (ConstBoundaryIterator boundaryItr = this->boundaryBegin(); 
         boundaryItr != this->boundaryEnd();
         ++boundaryItr) (*boundaryItr)->finalizeGhostBoundary();
  }
}

//------------------------------------------------------------------------------
// Finalize the hydro.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
finalize(const typename Dimension::Scalar time,
         const typename Dimension::Scalar dt,
         DataBase<Dimension>& dataBase,
         State<Dimension>& state,
         StateDerivatives<Dimension>& derivs) {

  // Base class finalization.
  GenericHydro<Dimension>::finalize(time, dt, dataBase, state, derivs);

  // Depending on the mass density advancement selected, we may want to replace the 
  // mass density.
  if (densityUpdate() == RigorousSumDensity or
      densityUpdate() == SumVoronoiCellDensity) {
    const ConnectivityMap<Dimension>& connectivityMap = dataBase.connectivityMap();
    const FieldList<Dimension, Vector> position = state.fields(HydroFieldNames::position, Vector::zero);
    const FieldList<Dimension, Scalar> mass = state.fields(HydroFieldNames::mass, 0.0);
    const FieldList<Dimension, Scalar> volume = state.fields(HydroFieldNames::volume, 0.0);
    const FieldList<Dimension, SymTensor> H = state.fields(HydroFieldNames::H, SymTensor::zero);
    FieldList<Dimension, Scalar> massDensity = state.fields(HydroFieldNames::massDensity, 0.0);
    computeSumVoronoiCellMassDensity(connectivityMap, this->kernel(), position, mass, volume, H, massDensity);
  } else if (densityUpdate() == SumDensity) {
    FieldList<Dimension, Scalar> massDensity = state.fields(HydroFieldNames::massDensity, 0.0);
    FieldList<Dimension, Scalar> massDensitySum = derivs.fields(ReplaceState<Dimension, Field<Dimension, Field<Dimension, Scalar> > >::prefix() + 
                                                                HydroFieldNames::massDensity, 0.0);
    massDensity.assignFields(massDensitySum);
  } else if (densityUpdate() == VoronoiCellDensity) {
    const FieldList<Dimension, Scalar> mass = state.fields(HydroFieldNames::mass, 0.0);
    const FieldList<Dimension, Scalar> volume = state.fields(HydroFieldNames::volume, 0.0);
    FieldList<Dimension, Scalar> massDensity = state.fields(HydroFieldNames::massDensity, 0.0);
    massDensity = mass / volume;
  }
}

//------------------------------------------------------------------------------
// Apply the ghost boundary conditions for hydro state fields.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
applyGhostBoundaries(State<Dimension>& state,
                     StateDerivatives<Dimension>& derivs) {

  // Apply boundary conditions to the basic fluid state Fields.
  FieldList<Dimension, Scalar> mass = state.fields(HydroFieldNames::mass, 0.0);
  FieldList<Dimension, Scalar> massDensity = state.fields(HydroFieldNames::massDensity, 0.0);
  FieldList<Dimension, Scalar> specificThermalEnergy = state.fields(HydroFieldNames::specificThermalEnergy, 0.0);
  FieldList<Dimension, Vector> velocity = state.fields(HydroFieldNames::velocity, Vector::zero);
  FieldList<Dimension, Scalar> pressure = state.fields(HydroFieldNames::pressure, 0.0);
  FieldList<Dimension, Scalar> soundSpeed = state.fields(HydroFieldNames::soundSpeed, 0.0);
  FieldList<Dimension, Scalar> volume = state.fields(HydroFieldNames::volume, 0.0);
  FieldList<Dimension, Scalar> A = state.fields(SVPHFieldNames::A_SVPH, 0.0);

  FieldList<Dimension, Scalar> specificThermalEnergy0;
  if (compatibleEnergyEvolution()) {
    CHECK(state.fieldNameRegistered(HydroFieldNames::specificThermalEnergy + "0"));
    specificThermalEnergy0 = state.fields(HydroFieldNames::specificThermalEnergy + "0", 0.0);
  }

  for (ConstBoundaryIterator boundaryItr = this->boundaryBegin(); 
       boundaryItr != this->boundaryEnd();
       ++boundaryItr) {
    (*boundaryItr)->applyFieldListGhostBoundary(mass);
    (*boundaryItr)->applyFieldListGhostBoundary(massDensity);
    (*boundaryItr)->applyFieldListGhostBoundary(specificThermalEnergy);
    (*boundaryItr)->applyFieldListGhostBoundary(velocity);
    (*boundaryItr)->applyFieldListGhostBoundary(pressure);
    (*boundaryItr)->applyFieldListGhostBoundary(soundSpeed);
    (*boundaryItr)->applyFieldListGhostBoundary(volume);
    (*boundaryItr)->applyFieldListGhostBoundary(A);
    if (compatibleEnergyEvolution()) (*boundaryItr)->applyFieldListGhostBoundary(specificThermalEnergy0);
  }
}

//------------------------------------------------------------------------------
// Enforce the boundary conditions for hydro state fields.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
enforceBoundaries(State<Dimension>& state,
                  StateDerivatives<Dimension>& derivs) {

  // Enforce boundary conditions on the fluid state Fields.
  FieldList<Dimension, Scalar> mass = state.fields(HydroFieldNames::mass, 0.0);
  FieldList<Dimension, Scalar> massDensity = state.fields(HydroFieldNames::massDensity, 0.0);
  FieldList<Dimension, Scalar> specificThermalEnergy = state.fields(HydroFieldNames::specificThermalEnergy, 0.0);
  FieldList<Dimension, Vector> velocity = state.fields(HydroFieldNames::velocity, Vector::zero);
  FieldList<Dimension, Scalar> pressure = state.fields(HydroFieldNames::pressure, 0.0);
  FieldList<Dimension, Scalar> soundSpeed = state.fields(HydroFieldNames::soundSpeed, 0.0);
  FieldList<Dimension, Scalar> volume = state.fields(HydroFieldNames::volume, 0.0);
  FieldList<Dimension, Scalar> A = state.fields(SVPHFieldNames::A_SVPH, 0.0);

  FieldList<Dimension, Scalar> specificThermalEnergy0;
  if (compatibleEnergyEvolution()) specificThermalEnergy0 = state.fields(HydroFieldNames::specificThermalEnergy + "0", 0.0);

  for (ConstBoundaryIterator boundaryItr = this->boundaryBegin(); 
       boundaryItr != this->boundaryEnd();
       ++boundaryItr) {
    (*boundaryItr)->enforceFieldListBoundary(mass);
    (*boundaryItr)->enforceFieldListBoundary(massDensity);
    (*boundaryItr)->enforceFieldListBoundary(specificThermalEnergy);
    (*boundaryItr)->enforceFieldListBoundary(velocity);
    (*boundaryItr)->enforceFieldListBoundary(pressure);
    (*boundaryItr)->enforceFieldListBoundary(soundSpeed);
    (*boundaryItr)->applyFieldListGhostBoundary(volume);
    (*boundaryItr)->applyFieldListGhostBoundary(A);
    if (compatibleEnergyEvolution()) (*boundaryItr)->enforceFieldListBoundary(specificThermalEnergy0);
  }
}

//------------------------------------------------------------------------------
// Dump the current state to the given file.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
dumpState(FileIO& file, const string& pathName) const {
  file.write(mTimeStepMask, pathName + "/timeStepMask");
  file.write(mPressure, pathName + "/pressure");
  file.write(mSoundSpeed, pathName + "/soundSpeed");
  file.write(mVolume, pathName + "/volume");
  file.write(mSpecificThermalEnergy0, pathName + "/specificThermalEnergy0");
  file.write(mHideal, pathName + "/Hideal");
  file.write(mMassDensitySum, pathName + "/massDensitySum");
  file.write(mWeightedNeighborSum, pathName + "/weightedNeighborSum");
  file.write(mMassSecondMoment, pathName + "/massSecondMoment");
  file.write(mXSVPHDeltaV, pathName + "/XSVPHDeltaV");

  file.write(mDxDt, pathName + "/DxDt");
  file.write(mDvDt, pathName + "/DvDt");
  file.write(mDmassDensityDt, pathName + "/DmassDensityDt");
  file.write(mDspecificThermalEnergyDt, pathName + "/DspecificThermalEnergyDt");
  file.write(mDHDt, pathName + "/DHDt");
  file.write(mDvDx, pathName + "/DvDx");
  file.write(mInternalDvDx, pathName + "/internalDvDx");
  file.write(mMaxViscousPressure, pathName + "/maxViscousPressure");
}

//------------------------------------------------------------------------------
// Restore the state from the given file.
//------------------------------------------------------------------------------
template<typename Dimension>
void
SVPHHydroBase<Dimension>::
restoreState(const FileIO& file, const string& pathName) {
 
  file.read(mTimeStepMask, pathName + "/timeStepMask");
  file.read(mPressure, pathName + "/pressure");
  file.read(mSoundSpeed, pathName + "/soundSpeed");
  file.read(mVolume, pathName + "/volume");
  file.read(mSpecificThermalEnergy0, pathName + "/specificThermalEnergy0");
  file.read(mHideal, pathName + "/Hideal");
  file.read(mMassDensitySum, pathName + "/massDensitySum");
  file.read(mWeightedNeighborSum, pathName + "/weightedNeighborSum");
  file.read(mMassSecondMoment, pathName + "/massSecondMoment");
  file.read(mXSVPHDeltaV, pathName + "/XSVPHDeltaV");

  file.read(mDxDt, pathName + "/DxDt");
  file.read(mDvDt, pathName + "/DvDt");
  file.read(mDmassDensityDt, pathName + "/DmassDensityDt");
  file.read(mDspecificThermalEnergyDt, pathName + "/DspecificThermalEnergyDt");
  file.read(mDHDt, pathName + "/DHDt");
  file.read(mDvDx, pathName + "/DvDx");
  file.read(mInternalDvDx, pathName + "/internalDvDx");
  file.read(mMaxViscousPressure, pathName + "/maxViscousPressure");
}

}
