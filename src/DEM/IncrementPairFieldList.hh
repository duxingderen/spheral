//---------------------------------Spheral++----------------------------------//
// IncrementPairFieldList -- An implementation of FieldListUpdatePolicyBase appropriate for
// when 'ya just want to increment by derivatives:  x1 = x0 + A*dx/dt
//
// Created by JMO, Sun Oct 27 11:32:51 PDT 2013
//----------------------------------------------------------------------------//
#ifndef __Spheral_IncrementPairFieldList_hh__
#define __Spheral_IncrementPairFieldList_hh__

#include "DataBase/FieldListUpdatePolicyBase.hh"

namespace Spheral {

template<typename Dimension, typename ValueType>
class IncrementPairFieldList: public FieldListUpdatePolicyBase<Dimension, ValueType> {
public:
  //--------------------------- Public Interface ---------------------------//
  // pull up from the parent
  typedef typename  FieldListUpdatePolicyBase<Dimension, ValueType>::KeyType KeyType;

  // Constructors, destructor.
  IncrementPairFieldList();
  explicit IncrementPairFieldList(const std::string& depend0);
  IncrementPairFieldList(const std::string& depend0, const std::string& depend1);
  IncrementPairFieldList(const std::string& depend0, const std::string& depend1, const std::string& depend2);
  IncrementPairFieldList(const std::string& depend0, const std::string& depend1, const std::string& depend2, const std::string& depend3);
  IncrementPairFieldList(const std::string& depend0, const std::string& depend1, const std::string& depend2, const std::string& depend3, const std::string& depend4);
  IncrementPairFieldList(const std::string& depend0, const std::string& depend1, const std::string& depend2, const std::string& depend3, const std::string& depend4, const std::string& depend5);
  virtual ~IncrementPairFieldList();
  
  static const std::string prefix() { return "delta "; }
  
  bool operator==(const UpdatePolicyBase<Dimension>& rhs) const override;

  // Overload the methods describing how to update FieldLists.
  virtual void update(const KeyType& key,
                      State<Dimension>& state,
                      StateDerivatives<Dimension>& derivs,
                      const double multiplier,
                      const double t,
                      const double dt) override;

private:
  //--------------------------- Private Interface ---------------------------//
  IncrementPairFieldList(const IncrementPairFieldList& rhs);
  IncrementPairFieldList& operator=(const IncrementPairFieldList& rhs);

};

}

#else

// Forward declaration.
namespace Spheral {
  template<typename Dimension, typename ValueType> class IncrementPairFieldList;
}

#endif
