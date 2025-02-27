//---------------------------------Spheral++----------------------------------//
// Compute the volume from m/rho
//----------------------------------------------------------------------------//

#include "GSPH/computeSPHVolume.hh"
#include "Field/FieldList.hh"
#include "Neighbor/ConnectivityMap.hh"
#include "Kernel/TableKernel.hh"
#include "NodeList/NodeList.hh"

#include <limits.h>

namespace Spheral{

template<typename Dimension>
void
computeSPHVolume(const FieldList<Dimension, typename Dimension::Scalar>& mass,
                 const FieldList<Dimension, typename Dimension::Scalar>& massDensity,
                       FieldList<Dimension, typename Dimension::Scalar>& volume) {

  // Pre-conditions.
  const auto numNodeLists = volume.size();
  REQUIRE(massDensity.size() == numNodeLists);
  REQUIRE(mass.size() == numNodeLists);

  const auto tiny = std::numeric_limits<typename Dimension::Scalar>::epsilon();

  for (auto nodeListi = 0u; nodeListi < numNodeLists; ++nodeListi) {
    const auto n = volume[nodeListi]->numInternalElements();
#pragma omp parallel for
    for (auto i = 0u; i < n; ++i) {
      volume(nodeListi,i) =  mass(nodeListi,i)/std::max(massDensity(nodeListi,i),tiny);
    }
  }   
}     // function
}     // spheral namespace
