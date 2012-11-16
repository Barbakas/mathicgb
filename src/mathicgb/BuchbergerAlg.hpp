#ifndef MATHICGB_BUCHBERGER_ALG_GUARD
#define MATHICGB_BUCHBERGER_ALG_GUARD

#include "Reducer.hpp"
#include "FreeModuleOrder.hpp"
#include "SPairs.hpp"
#include "PolyBasis.hpp"
#include <mathic.h>
#include <memory>
#include <ostream>
#include <vector>

/// Calculates a classic Grobner basis using Buchberger's algorithm.
class BuchbergerAlg {
public:
  BuchbergerAlg(
    const Ideal& ideal,
    FreeModuleOrderType orderType,
    Reducer& reducer,
    int divisorLookupType,
    bool preferSparseReducers,
    size_t queueType);

  // Replaces the current basis with a Grobner basis of the same ideal.
  void computeGrobnerBasis();

  // How many S-pairs were not eliminated before reduction of the
  // corresponding S-polynomial.
  unsigned long long sPolyReductionCount() const {return mSPolyReductionCount;}

  // Returns the current basis.
  PolyBasis& basis() {return mBasis;}

  // Shows statistics on what the algorithm has done.
  void printStats(std::ostream& out) const;

  void printMemoryUse(std::ostream& out) const;

  size_t getMemoryUse() const;

  void setBreakAfter(unsigned int elements) {
    mBreakAfter = elements;
  }

  void setPrintInterval(unsigned int reductions) {
    mPrintInterval = reductions;
  }

  void setSPairGroupSize(unsigned int groupSize) {
    mSPairGroupSize = groupSize;
  }

  void setReducerMemoryQuantum(size_t memoryQuantum) {
    mReducer.setMemoryQuantum(memoryQuantum);
  }

  void setUseAutoTopReduction(bool value) {
    mUseAutoTopReduction = value;
  }

  void setUseAutoTailReduction(bool value) {
    mUseAutoTailReduction = value;
  }

private:
  unsigned int mBreakAfter;
  unsigned int mPrintInterval;
  unsigned int mSPairGroupSize;
  bool mUseAutoTopReduction;
  bool mUseAutoTailReduction;

  // Perform a step of the algorithm.
  void step();

  void autoTailReduce();

  void insertReducedPoly(std::unique_ptr<Poly> poly);

  // clears polynomials.
  void insertPolys(std::vector<std::unique_ptr<Poly> >& polynomials);

  const PolyRing& mRing;
  std::unique_ptr<FreeModuleOrder> mOrder;
  Reducer& mReducer;
  PolyBasis mBasis;
  SPairs mSPairs;
  mic::Timer mTimer;
  unsigned long long mSPolyReductionCount;
};

#endif
