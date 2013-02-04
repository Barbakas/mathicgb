#include "stdinc.h"
#include "F4MatrixBuilder2.hpp"

#include "LogDomain.hpp"
#include <tbb/tbb.h>

MATHICGB_DEFINE_LOG_DOMAIN(
  F4MatrixBuild2,
  "Displays statistics about F4 matrix construction."
);

class F4MatrixBuilder2::F4PreBlock {
public:
  typedef uint32 RowIndex;
  typedef uint32 ColIndex;
  typedef coefficient ExternalScalar;
  typedef SparseMatrix::Scalar Scalar;

  struct Row {
    const ColIndex* indices;
    const Scalar* scalars;
    const ExternalScalar* externalScalars;
    ColIndex entryCount;
  };

  RowIndex rowCount() const {return static_cast<RowIndex>(mRows.size());}

  Row row(const RowIndex row) const {
    MATHICGB_ASSERT(row < mRows.size());
    const auto& r = mRows[row];
    Row rr;
    rr.indices = mIndices.data() + r.indicesBegin;
    rr.entryCount = r.entryCount;
    if (r.externalScalars == 0) {
      rr.scalars = mScalars.data() + r.scalarsBegin;
      rr.externalScalars = 0;
    } else {
      rr.scalars = 0;
      rr.externalScalars = r.externalScalars;
    }
    return rr;
  }

  ColIndex* makeRowWithTheseScalars(const Poly& scalars) {
    MATHICGB_ASSERT(rowCount() < std::numeric_limits<RowIndex>::max());
    MATHICGB_ASSERT
      (scalars.termCount() < std::numeric_limits<ColIndex>::max());

    InternalRow row;
    row.indicesBegin = mIndices.size();
    row.scalarsBegin = std::numeric_limits<decltype(row.scalarsBegin)>::max();
    row.entryCount = static_cast<ColIndex>(scalars.termCount());
    row.externalScalars = scalars.coefficientBegin();
    mRows.push_back(row);

    mIndices.resize(mIndices.size() + row.entryCount);
    return mIndices.data() + row.indicesBegin;
  }

  std::pair<ColIndex*, Scalar*> makeRow(ColIndex entryCount) {
    MATHICGB_ASSERT(rowCount() < std::numeric_limits<RowIndex>::max());

    InternalRow row;
    row.indicesBegin = mIndices.size();
    row.scalarsBegin = mScalars.size();
    row.entryCount = entryCount;
    row.externalScalars = 0;
    mRows.push_back(row);

    mIndices.resize(mIndices.size() + entryCount);
    mScalars.resize(mScalars.size() + entryCount);
    return std::make_pair(
      mIndices.data() + row.indicesBegin,
      mScalars.data() + row.scalarsBegin
    );
  }

  void removeLastEntries(const RowIndex row, const ColIndex count) {
    MATHICGB_ASSERT(row < rowCount());
    MATHICGB_ASSERT(mRows[row].entryCount >= count);
    mRows[row].entryCount -= count;
    if (row != rowCount() - 1)
      return;
    mIndices.resize(mIndices.size() - count);
    if (mRows[row].externalScalars == 0)
      mScalars.resize(mScalars.size() - count);
  }

private:
  struct InternalRow {
    size_t indicesBegin;
    size_t scalarsBegin;
    ColIndex entryCount;
    const ExternalScalar* externalScalars;
  };

  std::vector<ColIndex> mIndices;
  std::vector<Scalar> mScalars;
  std::vector<InternalRow> mRows;
};

void toSparseMatrix(const F4MatrixBuilder2::F4PreBlock& block, SparseMatrix& matrix) {
  typedef F4MatrixBuilder2::F4PreBlock F4PreBlock;
  const auto rowCount = block.rowCount();
  for (F4PreBlock::RowIndex r = 0; r < rowCount; ++r) {
    const auto row = block.row(r);
    const auto entryCount = row.entryCount;
    const F4PreBlock::ColIndex* const indices = row.indices;
    MATHICGB_ASSERT(row.scalars == 0 || row.externalScalars == 0);
    if (row.scalars != 0) {
      const F4PreBlock::Scalar* const scalars = row.scalars;
      for (F4PreBlock::ColIndex col = 0; col < entryCount; ++col)
        matrix.appendEntry(indices[col], scalars[col]);
    } else if (row.externalScalars != 0) {
      const F4PreBlock::ExternalScalar* const scalars = row.externalScalars;
      for (F4PreBlock::ColIndex col = 0; col < entryCount; ++col) {
        const auto scalar = static_cast<F4PreBlock::Scalar>(scalars[col]);
        MATHICGB_ASSERT
          (static_cast<F4PreBlock::ExternalScalar>(scalar) == scalar);
        matrix.appendEntry(indices[col], scalar);
      }
    }
    matrix.rowDone();
  }
}

MATHICGB_NO_INLINE
std::pair<F4MatrixBuilder2::ColIndex, ConstMonomial>
F4MatrixBuilder2::findOrCreateColumn(
  const const_monomial monoA,
  const const_monomial monoB,
  TaskFeeder& feeder
) {
  MATHICGB_ASSERT(!monoA.isNull());
  MATHICGB_ASSERT(!monoB.isNull());
  const auto col = ColReader(mMap).findProduct(monoA, monoB);
  if (col.first != 0)
    return std::make_pair(*col.first, col.second);
  return createColumn(monoA, monoB, feeder);
}

MATHICGB_INLINE
std::pair<F4MatrixBuilder2::ColIndex, ConstMonomial>
F4MatrixBuilder2::findOrCreateColumn(
  const const_monomial monoA,
  const const_monomial monoB,
  const ColReader& colMap,
  TaskFeeder& feeder
) {
  MATHICGB_ASSERT(!monoA.isNull());
  MATHICGB_ASSERT(!monoB.isNull());
  const auto col = colMap.findProduct(monoA, monoB);
  if (col.first == 0)
    return findOrCreateColumn(monoA, monoB, feeder);
  return std::make_pair(*col.first, col.second);
}

MATHICGB_NO_INLINE
void F4MatrixBuilder2::createTwoColumns(
  const const_monomial monoA1,
  const const_monomial monoA2,
  const const_monomial monoB,
  TaskFeeder& feeder
) {
  createColumn(monoA1, monoB, feeder);
  createColumn(monoA2, monoB, feeder);
}

F4MatrixBuilder2::F4MatrixBuilder2(
  const PolyBasis& basis,
  const size_t memoryQuantum
):
  mMemoryQuantum(memoryQuantum),
  mTmp(basis.ring().allocMonomial()),
  mBasis(basis),
  mMap(basis.ring()),
  mLeftColCount(0),
  mRightColCount(0)
{
  // This assert has to be _NO_ASSUME since otherwise the compiler will assume
  // that the error checking branch here cannot be taken and optimize it away.
  const Scalar maxScalar = std::numeric_limits<Scalar>::max();
  MATHICGB_ASSERT_NO_ASSUME(ring().charac() <= maxScalar);
  if (ring().charac() > maxScalar)
    mathic::reportInternalError("F4MatrixBuilder2: too large characteristic.");
}

void F4MatrixBuilder2::addSPolynomialToMatrix(
  const Poly& polyA,
  const Poly& polyB
) {
  MATHICGB_ASSERT(!polyA.isZero());
  MATHICGB_ASSERT(polyA.isMonic());
  MATHICGB_ASSERT(!polyB.isZero());
  MATHICGB_ASSERT(polyB.isMonic());

  RowTask task;
  task.poly = &polyA;
  task.sPairPoly = &polyB;
  mTodo.push_back(task);
}

void F4MatrixBuilder2::addPolynomialToMatrix(const Poly& poly) {
  if (poly.isZero())
    return;

  RowTask task = {};
  task.poly = &poly;
  mTodo.push_back(task);
}

void F4MatrixBuilder2::addPolynomialToMatrix
(const_monomial multiple, const Poly& poly) {
  MATHICGB_ASSERT(ring().hashValid(multiple));
  if (poly.isZero())
    return;

  RowTask task = {};
  task.poly = &poly;
  task.desiredLead = ring().allocMonomial();
  ring().monomialMult(poly.getLeadMonomial(), multiple, task.desiredLead);
  MATHICGB_ASSERT(ring().hashValid(task.desiredLead));

  MATHICGB_ASSERT(task.sPairPoly == 0);
  mTodo.push_back(task);
}

namespace {
  class ColumnComparer {
  public:
    ColumnComparer(const PolyRing& ring): mRing(ring) {}

    typedef SparseMatrix::ColIndex ColIndex;
    typedef std::pair<monomial, ColIndex> Pair;
    bool operator()(const Pair& a, const Pair b) const {
      return mRing.monomialLT(b.first, a.first);
    }

  private:
    const PolyRing& mRing;
  };

  std::vector<SparseMatrix::ColIndex> sortColumnMonomialsAndMakePermutation(
    std::vector<monomial>& monomials,
    const PolyRing& ring
  ) {
    typedef SparseMatrix::ColIndex ColIndex;
    MATHICGB_ASSERT(monomials.size() <= std::numeric_limits<ColIndex>::max());
    const ColIndex colCount = static_cast<ColIndex>(monomials.size());
    // Monomial needs to be non-const as we are going to put these
    // monomials back into the vector of monomials which is not const.
    std::vector<std::pair<monomial, ColIndex>> columns;
    columns.reserve(colCount);
    for (ColIndex col = 0; col < colCount; ++col)
      columns.push_back(std::make_pair(monomials[col], col));
    std::sort(columns.begin(), columns.end(), ColumnComparer(ring));

    // Apply sorting permutation to monomials. This is why it is necessary to
    // copy the values in monomial out of there: in-place application of a
    // permutation is messy.
    MATHICGB_ASSERT(columns.size() == colCount);
    MATHICGB_ASSERT(monomials.size() == colCount);
    for (size_t col = 0; col < colCount; ++col) {
      MATHICGB_ASSERT(col == 0 ||
        ring.monomialLT(columns[col].first, columns[col - 1].first));
      monomials[col] = columns[col].first;
    }

    // Construct permutation of indices to match permutation of monomials
    std::vector<ColIndex> permutation(colCount);
    for (ColIndex col = 0; col < colCount; ++col) {
      // The monomial for column columns[col].second is now the
      // monomial for col, so we need the inverse map for indices.
      permutation[columns[col].second] = col;
    }

    return std::move(permutation);
  }
}

void F4MatrixBuilder2::buildMatrixAndClear(QuadMatrix& quadMatrix) {
  MATHICGB_LOG_TIME(F4MatrixBuild2) <<
    "\n***** Constructing matrix *****\n";

  if (mTodo.empty()) {
    quadMatrix = QuadMatrix();
    quadMatrix.ring = &ring();
    return;
  }

  // todo: prefer sparse/old reducers among the inputs.

  // Process pending rows until we are done. Note that the methods
  // we are calling here can add more pending items.

  struct ThreadData {
    F4PreBlock block;
    monomial tmp1;
    monomial tmp2;
  };

  tbb::enumerable_thread_specific<ThreadData> threadData([&](){  
    ThreadData data;
    {
      tbb::mutex::scoped_lock guard(mCreateColumnLock);
      data.tmp1 = ring().allocMonomial();
      data.tmp2 = ring().allocMonomial();
    }
    return std::move(data);
  });

  tbb::parallel_do(mTodo.begin(), mTodo.end(),
    [&](const RowTask& task, TaskFeeder& feeder)
  {
    auto& data = threadData.local();
    const auto& poly = *task.poly;

    if (task.sPairPoly != 0) {
      ring().monomialColons(
        poly.getLeadMonomial(),
        task.sPairPoly->getLeadMonomial(),
        data.tmp2,
        data.tmp1
      );
      appendRowSPair
        (&poly, data.tmp1, task.sPairPoly, data.tmp2, data.block, feeder);
      return;
    }
    if (task.desiredLead.isNull())
      ring().monomialSetIdentity(data.tmp1);
    else
      ring().monomialDivide
        (task.desiredLead, poly.getLeadMonomial(), data.tmp1);
    MATHICGB_ASSERT(ring().hashValid(data.tmp1));
    appendRow(data.tmp1, *task.poly, data.block, feeder);
  });
  MATHICGB_ASSERT(!threadData.empty()); // as mTodo empty causes early return

  // Free the monomials from all the tasks
  const auto todoEnd = mTodo.end();
  for (auto it = mTodo.begin(); it != todoEnd; ++it)
    if (!it->desiredLead.isNull())
      ring().freeMonomial(it->desiredLead);
  mTodo.clear();

  {
    ColReader reader(mMap);
    quadMatrix.leftColumnMonomials.swap(Monomials(mLeftColCount));
    quadMatrix.rightColumnMonomials.swap(Monomials(mRightColCount));
    const auto end = reader.end();
    for (auto it = reader.begin(); it != end; ++it) {
      const auto p = *it;
      monomial copy = ring().allocMonomial();
      ring().monomialCopy(p.second, copy);
      const auto index = p.first;
      auto translated = mTranslate[index];
      auto& monos = translated.left ?
        quadMatrix.leftColumnMonomials : quadMatrix.rightColumnMonomials;
      MATHICGB_ASSERT(translated.index < monos.size());
      MATHICGB_ASSERT(monos[translated.index].isNull());
      monos[translated.index] = copy;
    }
  }

  typedef SparseMatrix::ColIndex ColIndex;
  std::vector<ColIndex> leftPermutation;
  std::vector<ColIndex> rightPermutation;
  
  tbb::parallel_for(0, 2, 1, [&](int i) {
    if (i == 0)
      leftPermutation =
        sortColumnMonomialsAndMakePermutation(quadMatrix.leftColumnMonomials, ring());
    else 
      rightPermutation =
        sortColumnMonomialsAndMakePermutation(quadMatrix.rightColumnMonomials, ring());
  });

  MATHICGB_ASSERT(leftPermutation.size() + rightPermutation.size() == mTranslate.size());
  for (size_t i = 0; i < mTranslate.size(); ++i) {
    if (mTranslate[i].left)
      mTranslate[i].index = leftPermutation[mTranslate[i].index];
    else
      mTranslate[i].index = rightPermutation[mTranslate[i].index];
  }

  // Decide which rows are reducers (top) and which are reducees (bottom).
  const auto noReducer = std::numeric_limits<RowIndex>::max();
  F4PreBlock::Row noRow = {};
  noRow.indices = 0;
  std::vector<F4PreBlock::Row> reducerRows(mLeftColCount, noRow);
  std::vector<F4PreBlock::Row> reduceeRows;

  SparseMatrix matrix(mMemoryQuantum);
  const auto end = threadData.end();
  for (auto it = threadData.begin(); it != end; ++it) {
    auto& block = it->block;
    const auto rowCount = block.rowCount();
    for (RowIndex r = 0; r < rowCount; ++r) {
      const auto row = block.row(r);
      if (row.entryCount == 0)
        continue;

      // Determine leading (minimum index) left entry.
      const auto lead = [&] {
        MATHICGB_ASSERT(mTranslate.size() <=
          std::numeric_limits<ColIndex>::max());
        const auto end = row.indices + row.entryCount;
        for (auto it = row.indices; it != end; ++it) {
          MATHICGB_ASSERT(*it < mTranslate.size());
          if (mTranslate[*it].left)
            return mTranslate[*it].index;
        }
        return mLeftColCount; // No left entries at all.
      }();
      if (!mTranslate[*row.indices].left) {
        reduceeRows.push_back(row); // no left entries
        continue; // todo: for now
      }
      // Decide if this should be a reducer or reducee row.
      if (lead == mLeftColCount) {
        reduceeRows.push_back(row); // no left entries
        continue;
      }
      auto& reducer = reducerRows[lead];
      if (reducer.entryCount == 0)
        reducer = row; // row is first reducer with this lead
      else if (reducer.entryCount > row.entryCount) {
        reduceeRows.push_back(reducer); // row sparser (=better) reducer
        reducer = row;
      } else
        reduceeRows.push_back(row);
    }

    ring().freeMonomial(it->tmp1);
    ring().freeMonomial(it->tmp2);
  }

  MATHICGB_ASSERT(reducerRows.size() == mLeftColCount);
#ifdef MATHICGB_DEBUG
  for (size_t  i = 0; i < reducerRows.size(); ++i) {
    const auto row = reducerRows[i];
    MATHICGB_ASSERT(row.entryCount > 0);
    MATHICGB_ASSERT(mTranslate[*row.indices].left);
    MATHICGB_ASSERT(mTranslate[*row.indices].index == i);
  }
#endif
 
  quadMatrix.ring = &ring();
  auto splitLeftRight = [this](
    const std::vector<F4PreBlock::Row>& from,
    const bool makeLeftUnitary,
    SparseMatrix& left,
    SparseMatrix& right
  ) {
    left.clear();
    right.clear();
    const auto fromEnd = from.end();
    for (auto fromIt = from.begin(); fromIt != fromEnd; ++fromIt) {
      const auto row = *fromIt;
      MATHICGB_ASSERT(row.entryCount != 0);
      MATHICGB_ASSERT(row.scalars == 0 || row.externalScalars == 0);

      if (row.externalScalars != 0) {
        auto indices = row.indices;
        auto indicesEnd = row.indices + row.entryCount;
        auto scalars = row.externalScalars;
        for (; indices != indicesEnd; ++indices, ++scalars) {
          const auto scalar = static_cast<Scalar>(*scalars);
          const auto index = *indices;
          const auto translated = mTranslate[index];
          if (translated.left)
            left.appendEntry(translated.index, scalar);
          else
            right.appendEntry(translated.index, scalar);
        }
      } else {
        auto indices = row.indices;
        auto indicesEnd = row.indices + row.entryCount;
        auto scalars = row.scalars;
        for (; indices != indicesEnd; ++indices, ++scalars) {
          const auto index = *indices;
          const auto translated = mTranslate[index];
          if (translated.left)
            left.appendEntry(translated.index, *scalars);
          else
            right.appendEntry(translated.index, *scalars);
        }
      }
      const auto rowIndex = left.rowCount();
      MATHICGB_ASSERT(rowIndex == right.rowCount());
      left.rowDone();
      right.rowDone();

      if (
        makeLeftUnitary &&
        !left.emptyRow(rowIndex) &&
        left.rowBegin(rowIndex).scalar() != 1
      ) {
        const auto modulus = static_cast<Scalar>(ring().charac());
        const auto inverse =
          modularInverse(left.rowBegin(rowIndex).scalar(), modulus);
        left.multiplyRow(rowIndex, inverse, modulus);
        right.multiplyRow(rowIndex, inverse, modulus);
        MATHICGB_ASSERT(left.rowBegin(rowIndex).scalar() == 1);
      }

      MATHICGB_ASSERT(left.rowCount() == right.rowCount());
      MATHICGB_ASSERT(!makeLeftUnitary || !left.emptyRow(left.rowCount() - 1));
      MATHICGB_ASSERT
        (!makeLeftUnitary || left.rowBegin(left.rowCount() - 1).scalar() == 1);
    }
  };
  splitLeftRight(reducerRows, true, quadMatrix.topLeft, quadMatrix.topRight);
  splitLeftRight
    (reduceeRows, false, quadMatrix.bottomLeft, quadMatrix.bottomRight);
  threadData.clear();

#ifdef MATHICGB_DEBUG
  for (size_t side = 0; side < 2; ++side) {
    auto& monos = side == 0 ?
      quadMatrix.leftColumnMonomials : quadMatrix.rightColumnMonomials;
    for (auto it = monos.begin(); it != monos.end(); ++it) {
      MATHICGB_ASSERT(!it->isNull());
    }
  }
  for (RowIndex row = 0; row < quadMatrix.topLeft.rowCount(); ++row) {
    MATHICGB_ASSERT(quadMatrix.topLeft.leadCol(row) == row);
  }
#endif

  // todo: do this together with left/right split
  //quadMatrix.sortColumnsLeftRightParallel();
#ifdef MATHICGB_DEBUG
  MATHICGB_ASSERT(quadMatrix.debugAssertValid());
#endif

  mMap.clearNonConcurrent();
}

std::pair<F4MatrixBuilder2::ColIndex, ConstMonomial>
F4MatrixBuilder2::createColumn(
  const const_monomial monoA,
  const const_monomial monoB,
  TaskFeeder& feeder
) {
  MATHICGB_ASSERT(!monoA.isNull());
  MATHICGB_ASSERT(!monoB.isNull());

  tbb::mutex::scoped_lock lock(mCreateColumnLock);
  // see if the column exists now after we have synchronized
  {
    const auto found(ColReader(mMap).findProduct(monoA, monoB));
    if (found.first != 0)
      return std::make_pair(*found.first, found.second);
  }

  // The column really does not exist, so we need to create it
  ring().monomialMult(monoA, monoB, mTmp);
  if (!ring().monomialHasAmpleCapacity(mTmp))
    mathic::reportError("Monomial exponent overflow in F4MatrixBuilder2.");
  MATHICGB_ASSERT(ring().hashValid(mTmp));

  // look for a reducer of mTmp
  const size_t reducerIndex = mBasis.divisor(mTmp);
  const bool insertLeft = (reducerIndex != static_cast<size_t>(-1));

  MATHICGB_ASSERT(mLeftColCount + mRightColCount == mTranslate.size());

  // Create the new left or right column
  auto& colCount = insertLeft ? mLeftColCount : mRightColCount;
  if (colCount == std::numeric_limits<ColIndex>::max())
    throw std::overflow_error("Too many columns in QuadMatrix");
  const auto newIndex = static_cast<ColIndex>(mTranslate.size());
  const auto inserted = mMap.insert(std::make_pair(mTmp, newIndex));
  Translated translated = {colCount, insertLeft};
  mTranslate.push_back(translated);
  ++colCount;

  MATHICGB_ASSERT(mLeftColCount + mRightColCount == mTranslate.size());

  // schedule new task if we found a reducer
  if (insertLeft) {
    RowTask task = {};
    task.poly = &mBasis.poly(reducerIndex);
    task.desiredLead = inserted.first.second.castAwayConst();
    feeder.add(task);
  }

  return std::make_pair(*inserted.first.first, inserted.first.second);
}

void F4MatrixBuilder2::appendRow(
  const const_monomial multiple,
  const Poly& poly,
  F4PreBlock& block,
  TaskFeeder& feeder
) {
  MATHICGB_ASSERT(!multiple.isNull());

  const auto begin = poly.begin();
  const auto end = poly.end();
  const auto count = std::distance(begin, end);
  MATHICGB_ASSERT(count < std::numeric_limits<ColIndex>::max());
  auto indices = block.makeRowWithTheseScalars(poly);

  auto it = begin;
  if ((count % 2) == 1) {
    ColReader reader(mMap);
    const auto col = findOrCreateColumn
      (it.getMonomial(), multiple, reader, feeder);
	MATHICGB_ASSERT(it.getCoefficient() < std::numeric_limits<Scalar>::max());
    MATHICGB_ASSERT(it.getCoefficient());
    //matrix.appendEntry(col.first, static_cast<Scalar>(it.getCoefficient()));
    *indices = col.first;
    ++indices;
    //*row.first++ = col.first;
    //*row.second++ = static_cast<Scalar>(it.getCoefficient());
    ++it;
  }
updateReader:
  ColReader colMap(mMap);
  MATHICGB_ASSERT((std::distance(it, end) % 2) == 0);
  while (it != end) {
	MATHICGB_ASSERT(it.getCoefficient() < std::numeric_limits<Scalar>::max());
    MATHICGB_ASSERT(it.getCoefficient() != 0);
    const auto scalar1 = static_cast<Scalar>(it.getCoefficient());
    const const_monomial mono1 = it.getMonomial();

    auto it2 = it;
    ++it2;
	MATHICGB_ASSERT(it2.getCoefficient() < std::numeric_limits<Scalar>::max());
    MATHICGB_ASSERT(it2.getCoefficient() != 0);
    const auto scalar2 = static_cast<Scalar>(it2.getCoefficient());
    const const_monomial mono2 = it2.getMonomial();

    const auto colPair = colMap.findTwoProducts(mono1, mono2, multiple);
    if (colPair.first == 0 || colPair.second == 0) {
      createTwoColumns(mono1, mono2, multiple, feeder);
      goto updateReader;
    }

    //matrix.appendEntry(*colPair.first, scalar1);
    //matrix.appendEntry(*colPair.second, scalar2);

    *indices = *colPair.first;
    ++indices;
    *indices = *colPair.second;
    ++indices;

    //*row.first++ = *colPair.first;
    //*row.second++ = scalar1;
    //*row.first++ = *colPair.second;
    //*row.second++ = scalar2;

    it = ++it2;
  }
  //matrix.rowDone();
}

void F4MatrixBuilder2::appendRowSPair(
  const Poly* poly,
  monomial multiply,
  const Poly* sPairPoly,
  monomial sPairMultiply,
  F4PreBlock& block,
  TaskFeeder& feeder
) {
  MATHICGB_ASSERT(!poly->isZero());
  MATHICGB_ASSERT(!multiply.isNull());
  MATHICGB_ASSERT(ring().hashValid(multiply));
  MATHICGB_ASSERT(sPairPoly != 0);
  Poly::const_iterator itA = poly->begin();
  const Poly::const_iterator endA = poly->end();

  MATHICGB_ASSERT(!sPairPoly->isZero());
  MATHICGB_ASSERT(!sPairMultiply.isNull());
  MATHICGB_ASSERT(ring().hashValid(sPairMultiply));
  Poly::const_iterator itB = sPairPoly->begin();
  Poly::const_iterator endB = sPairPoly->end();

  // skip leading terms since they cancel
  MATHICGB_ASSERT(itA.getCoefficient() == itB.getCoefficient());
  ++itA;
  ++itB;

  // @todo: handle overflow of termCount addition here
  MATHICGB_ASSERT(poly->termCount() + sPairPoly->termCount() - 2 <=
    std::numeric_limits<ColIndex>::max());
  const auto maxCols =
    static_cast<ColIndex>(poly->termCount() + sPairPoly->termCount() - 2);
  auto row = block.makeRow(maxCols);
  const auto indicesBegin = row.first;

  const ColReader colMap(mMap);

  const const_monomial mulA = multiply;
  const const_monomial mulB = sPairMultiply;
  while (itB != endB && itA != endA) {
    const auto colA = findOrCreateColumn
      (itA.getMonomial(), mulA, colMap, feeder);
    const auto colB = findOrCreateColumn
      (itB.getMonomial(), mulB, colMap, feeder);
    const auto cmp = ring().monomialCompare(colA.second, colB.second);

    coefficient coeff = 0;
    ColIndex col;
    if (cmp != LT) {
      coeff = itA.getCoefficient();
      col = colA.first;
      ++itA;
    }
    if (cmp != GT) {
      coeff = ring().coefficientSubtract(coeff, itB.getCoefficient());
      col = colB.first;
      ++itB;
    }
    MATHICGB_ASSERT(coeff < std::numeric_limits<Scalar>::max());
    if (coeff != 0) {
      //matrix.appendEntry(col, static_cast<Scalar>(coeff));
      *row.first++ = col;
      *row.second++ = static_cast<Scalar>(coeff);
    }
  }

  for (; itA != endA; ++itA) {
    const auto colA = findOrCreateColumn
      (itA.getMonomial(), mulA, colMap, feeder);
    //matrix.appendEntry(colA.first, static_cast<Scalar>(itA.getCoefficient()));
    *row.first++ = colA.first;
    *row.second++ = static_cast<Scalar>(itA.getCoefficient());
  }

  for (; itB != endB; ++itB) {
    const auto colB = findOrCreateColumn
      (itB.getMonomial(), mulB, colMap, feeder);
    const auto negative = ring().coefficientNegate(itB.getCoefficient());
    //matrix.appendEntry(colB.first, static_cast<Scalar>(negative));
    *row.first++ = colB.first;
    *row.second++ = static_cast<Scalar>(negative);
  }

  const auto toRemove =
    maxCols - static_cast<ColIndex>(row.first - indicesBegin);
  block.removeLastEntries(block.rowCount() - 1, toRemove);
}