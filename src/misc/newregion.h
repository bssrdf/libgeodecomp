#ifndef _libgeodecomp_misc_newregion_h_
#define _libgeodecomp_misc_newregion_h_

#include <algorithm>

#include <libgeodecomp/misc/coordbox.h>
#include <libgeodecomp/misc/outputpairs.h>
#include <libgeodecomp/misc/streak.h>
#include <libgeodecomp/misc/supermap.h>
#include <libgeodecomp/misc/supervector.h>

namespace LibGeoDecomp {

template<int DIM>
class StreakIteratorHelper
{
public:
    typedef std::pair<int, int> IntPair;
    typedef SuperVector<IntPair> VecType;
    
    template<int STREAK_DIM>
    inline void initBegin(Streak<STREAK_DIM> *streak, VecType::const_iterator *iterators, const VecType *vectors)
    {
        StreakIteratorHelper<DIM - 1>().initBegin(streak, iterators, vectors);
        iterators[DIM] = vectors[DIM].begin();

        if (vectors[DIM].size() > 0) {
            streak->origin[DIM] = vectors[DIM][0].first;
        }
    }

    template<int STREAK_DIM>
    inline void initEnd(Streak<STREAK_DIM> *streak, VecType::const_iterator *iterators, const VecType *vectors)
    {
        StreakIteratorHelper<DIM - 1>().initEnd(streak, iterators, vectors);
        iterators[DIM] = vectors[DIM].end();
    }

    inline bool compareIterators(const VecType::const_iterator *a, const VecType::const_iterator *b)
    {
        if (a[DIM] != b[DIM]) {
            return false;
        }
        
        return StreakIteratorHelper<DIM - 1>().compareIterators(a, b);
    }
};

template<>
class StreakIteratorHelper<0>
{
public:
    typedef std::pair<int, int> IntPair;
    typedef SuperVector<IntPair> VecType;
    
    template<int STREAK_DIM>
    inline void initBegin(Streak<STREAK_DIM> *streak, VecType::const_iterator *iterators, const VecType *vectors)
    {
        iterators[0] = vectors[0].begin();

        if (vectors[0].size() > 0) {
            streak->endX = vectors[0][0].second;
            streak->origin[0] = vectors[0][0].first;
        }
    }

    template<int STREAK_DIM>
    inline void initEnd(Streak<STREAK_DIM> *streak, VecType::const_iterator *iterators, const VecType *vectors)
    {
        iterators[0] = vectors[0].end();
    }

    inline bool compareIterators(const VecType::const_iterator *a, const VecType::const_iterator *b)
    {
        return a[0] == b[0];
    }
};

class NewRegionCommonHelper
{
protected:
    typedef std::pair<int, int> IntPair;
    typedef SuperVector<IntPair> VecType;

    inline void incRemainder(const VecType::iterator& start, const VecType::iterator& end, const int& inserts)
    {
        if (inserts == 0) {
            return;
        }

        for (VecType::iterator incrementer = start; 
             incrementer != end; ++incrementer) {
            incrementer->second += inserts;
        }
    }
};

template<int DIM> 
class NewRegionInsertHelper;

template<int DIM> 
class NewRegionRemoveHelper;

/**
 * NewRegion stores a set of coordinates. It performs a run-length
 * coding. Instead of storing complete Streak objects, these objects
 * get split up and are stored implicitly in the hierarchical indices
 * vectors.
 */
template<int DIM>
class NewRegion
{
    template<int MY_DIM> friend class NewRegionInsertHelper;
    template<int MY_DIM> friend class NewRegionRemoveHelper;
    friend class NewRegionTest;
public:
    typedef std::pair<int, int> IntPair;
    typedef SuperVector<IntPair> VecType;

    class StreakIterator : public std::iterator<std::forward_iterator_tag, const Streak<DIM> >
    {
        template<int> friend class InitIterators;
        template<int> friend class NewRegion;
    public:
        typedef std::pair<int, int> IntPair;
        typedef SuperVector<IntPair> VecType;

        inline StreakIterator(const NewRegion *_region) :
            region(_region)
        {}

        inline void operator++()
        {
            ++iterators[0];
            streak.origin[0] = iterators[0]->first;
            streak.endX = iterators[0]->second;
            if (iterators[0] == region->indices[0].end()) {
                for (int i = 1; i < DIM; ++i) {
                    iterators[i] = region->indices[i].end();
                }
                return;
            }

            for (int i = 1; i < DIM; ++i) {
                VecType::const_iterator nextEnd = 
                    region->indices[i - 1].begin() + (iterators[i] + 1)->second;
;
                if (iterators[i - 1] != nextEnd) {
                    return;
                }

                ++iterators[i];
                streak.origin[i] = iterators[i]->first;
            }
        }

        inline bool operator==(const StreakIterator& other) const
        {
            return StreakIteratorHelper<DIM - 1>().compareIterators(iterators, other.iterators);
        }

        inline bool operator!=(const StreakIterator& other) const
        {
            return !(*this == other);
        }

        inline const Streak<DIM> operator*() const
        {
            return streak;
        }

        inline const Streak<DIM> *operator->() const
        {
            return &streak;
        }

        inline bool endReached() const
        {
            return iterators[0] == region->indices[0].end();
        }

    private:
        VecType::const_iterator iterators[DIM];
        Streak<DIM> streak;
        const NewRegion<DIM> *region;
    };

    class Iterator : public std::iterator<std::forward_iterator_tag, 
                                          const Coord<DIM> >
    {
    public:
        inline Iterator(const StreakIterator& _streakIterator) :
            streakIterator(_streakIterator)
        {
            cursor = _streakIterator->origin;
        }

        inline void operator++()
        {
            cursor.x()++;
            if (cursor.x() >= streakIterator->endX) {
                ++streakIterator;
                cursor = streakIterator->origin;   
            }             
        }

        inline bool operator==(const Iterator& other) const
        {
            return streakIterator == other.streakIterator &&
                (streakIterator.endReached() || cursor == other.cursor);
        }
            
        inline bool operator!=(const Iterator& other) const
        {
            return !(*this == other);
        }
        
        inline const Coord<DIM>& operator*() const
        {
            return cursor;
        }

        inline const Coord<DIM> *operator->() const
        {
            return &cursor;
        }

    private:
        StreakIterator streakIterator;
        Coord<DIM> cursor;
    };

    inline NewRegion() :
        mySize(0),
        geometryCacheTainted(false)
    {}

    template<class ITERATOR1, class ITERATOR2>
    inline NewRegion(const ITERATOR1& start, const ITERATOR2& end) :
        mySize(0),
        geometryCacheTainted(false)
    {
        load(start, end);
    }

    template<class ITERATOR1, class ITERATOR2>
    inline void load(const ITERATOR1& start, const ITERATOR2& end)
    {
        for (ITERATOR1 i = start; i != end; ++i) {
            *this << *i;
        }
    }

    inline void clear()
    {
        for (int i = 0; i < DIM; ++i) {
            indices[i].clear();
        }
        mySize = 0;
        myBoundingBox = CoordBox<DIM>();
        geometryCacheTainted = false;
    }

    inline const CoordBox<DIM>& boundingBox() const
    {
        if (geometryCacheTainted)
            resetGeometryCache();
        return myBoundingBox;
    }

    inline const long& size() const
    {
        if (geometryCacheTainted)
            resetGeometryCache();
        return mySize;
    }

    inline const unsigned numStreaks() const
    {
        return indices[0].size();
    }

    inline NewRegion expand(const unsigned& width=1) const
    {
        NewRegion ret;
        Coord<DIM> dia = Coord<DIM>::diagonal(width);

        for (StreakIterator i = beginStreak(); i != endStreak(); ++i) {
            Streak<DIM> streak = *i;

            Coord<DIM> boxOrigin = streak.origin - dia;
            Coord<DIM> boxDim = Coord<DIM>::diagonal(2 * width + 1);
            boxDim.x() = 1;
            int endX = streak.endX + width;
            CoordBox<DIM> box(boxOrigin, boxDim);

            for (typename CoordBox<DIM>::Iterator i = box.begin(); i != box.end(); ++i) {
                ret << Streak<DIM>(*i, endX);
            }
        }

        return ret;
    }

    /**
     * does the same as expand, but will wrap overlap at edges
     * correctly. The instance of the TOPOLOGY is actually unused, but
     * without it g++ would complain...
     */
    template<typename TOPOLOGY>
    inline NewRegion expandWithTopology(
        const unsigned& width, 
        const Coord<DIM>& dimensions, 
        TOPOLOGY /* unused */) const
    {
        NewRegion ret;
        Coord<DIM> dia = Coord<DIM>::diagonal(width);

        for (StreakIterator i = beginStreak(); i != endStreak(); ++i) {
            Streak<DIM> streak = *i;

            Coord<DIM> boxOrigin = streak.origin - dia;
            Coord<DIM> boxDim = Coord<DIM>::diagonal(2 * width + 1);
            boxDim.x() = 1;
            int endX = streak.endX + width;
            CoordBox<DIM> box(boxOrigin, boxDim);

            for (typename CoordBox<DIM>::Iterator i = box.begin(); i != box.end(); ++i) {
                Streak<DIM> newStreak(*i, endX);
                if (TOPOLOGY::wrapsAxis(0)) {
                    splitStreak<TOPOLOGY>(newStreak, &ret, dimensions);
                } else {
                    normalizeStreak<TOPOLOGY>(
                        trimStreak(newStreak, dimensions), &ret, dimensions);
                }
            }
        }

        return ret;
    }
       
    inline bool operator==(const NewRegion<DIM>& other) const
    {
        for (int i = 0; i < DIM; ++i) {
            if (indices[i] != other.indices[i]) {
                return false;
            }
        }
            
        return true;
    }

    inline NewRegion& operator<<(const Streak<DIM>& s)
    {
        //ignore 0 length streaks
        if (s.endX <= s.origin.x()) {
            return *this;
        }

        geometryCacheTainted = true;
        NewRegionInsertHelper<DIM - 1>()(this, s);
        return *this;
    }

    inline NewRegion& operator<<(const Coord<DIM>& c)
    {
        *this << Streak<DIM>(c, c.x() + 1);
        return *this;
    }

    inline NewRegion& operator<<(CoordBox<DIM> box)
    {
        int width = box.dimensions.x();
        box.dimensions.x() = 1;

        for (typename CoordBox<DIM>::Iterator i = box.begin(); i != box.end(); ++i) {
            *this << Streak<DIM>(*i, i->x() + width);
        }
        
        return *this;
    }

    inline NewRegion& operator>>(const Streak<DIM>& s)
    { 
        //ignore 0 length streaks and empty selves
        if (s.endX <= s.origin.x() || empty()) {
            return *this;
        }

        geometryCacheTainted = true;
        NewRegionRemoveHelper<DIM - 1>()(this, s);
        return *this;
    }

    inline NewRegion& operator>>(const Coord<DIM>& c)
    { 
        *this >> Streak<DIM>(c, c.x() + 1);
        return *this;
    }

    inline void operator-=(const NewRegion& other) 
    {
        for (StreakIterator i = other.beginStreak(); i != other.endStreak(); ++i) {
            *this >> *i;
        }
    }
    
    inline NewRegion operator-(const NewRegion& other) const
    {
        NewRegion ret(*this);
        ret -= other;
        return ret;
    }
    
    inline void operator&=(const NewRegion& other) 
    {
        NewRegion excess(*this);
        excess -= other;
        *this -= excess;
    }
    
    inline NewRegion operator&(const NewRegion& other) const
    {
        NewRegion ret(*this);
        ret &= other;
        return ret;
    }

    inline void operator+=(const NewRegion& other)
    {
        for (StreakIterator i = other.beginStreak(); i != other.endStreak(); ++i) {
            *this << *i;
        }
    }

    inline NewRegion operator+(const NewRegion& other) const
    {
        NewRegion ret(*this);
        ret += other;
        return ret;
    }

    inline SuperVector<Streak<DIM> > toVector() const
    {
        SuperVector<Streak<DIM> > ret(numStreaks());
        std::copy(beginStreak(), endStreak(), ret.begin());
        return ret;
    }
    
    inline std::string toString() const
    {
        std::ostringstream buf;
        buf << "NewRegion(\n";
        for (int dim = 0; dim < DIM; ++dim) {
            buf << "indices[" << dim << "] = " 
                << indices[dim] << "\n";
        }
        buf << ")\n";

        return buf.str();

    }

    inline bool empty() const
    {
        return (indices[0].size() == 0);
    }

    inline StreakIterator beginStreak() const
    {
        StreakIterator ret(this);
        StreakIteratorHelper<DIM - 1>().initBegin(&ret.streak, ret.iterators, indices);
        return ret;
    }

    inline StreakIterator endStreak() const
    {
        StreakIterator ret(this);
        StreakIteratorHelper<DIM - 1>().initEnd(&ret.streak, ret.iterators, indices);
        return ret;
    }

    inline Iterator begin() const
    {
        return Iterator(beginStreak());
    }
    
    inline Iterator end() const
    {
        return Iterator(endStreak());
    }

private:
    VecType indices[DIM];
    mutable CoordBox<DIM> myBoundingBox;
    mutable long mySize;
    mutable bool geometryCacheTainted;

    inline void determineGeometry() const
    {
        if (empty()) {
            myBoundingBox = CoordBox<DIM>();
        } else {
            const Streak<DIM>& someStreak = *beginStreak();
            Coord<DIM> min = someStreak.origin;
            Coord<DIM> max = someStreak.origin;

            mySize = 0;
            for (StreakIterator i = beginStreak(); 
                 i != endStreak(); ++i) {
                Coord<DIM> left = i->origin;
                Coord<DIM> right = i->origin;
                right.x() = i->endX - 1;

                min = min.min(left);
                max = max.max(right);
                mySize += i->endX - i->origin.x();
            }
            myBoundingBox = 
                CoordBox<DIM>(min, max - min + Coord<DIM>::diagonal(1));
        }
    }

    inline void resetGeometryCache() const
    {
        determineGeometry();       
        geometryCacheTainted = false;
    }

    inline Streak<DIM> trimStreak(
        const Streak<DIM>& s, 
        const Coord<DIM>& dimensions) const
    {
        int width = dimensions.x();
        Streak<DIM> buf = s;
        buf.origin.x() = std::max(buf.origin.x(), 0);
        buf.endX = std::min(width, buf.endX);
        return buf;
    }

    template<typename TOPOLOGY>
    void splitStreak(
        const Streak<DIM>& streak, 
        NewRegion *target, 
        const Coord<DIM>& dimensions) const 
    {
        int width = dimensions.x();

        int currentX = streak.origin.x();
        if (currentX < 0) {
            Streak<DIM> section = streak;
            section.endX = std::min(streak.endX, 0);
            currentX = section.endX;

            // normalize left overhang
            section.origin.x() += width;
            section.endX += width;
            normalizeStreak<TOPOLOGY>(section, target, dimensions);
        }

        if (currentX < streak.endX) {
            Streak<DIM> section = streak;
            section.origin.x() = currentX;
            section.endX = std::min(streak.endX, width);
            currentX = section.endX;

            normalizeStreak<TOPOLOGY>(section, target, dimensions);
        }

        if (currentX < streak.endX) {
            Streak<DIM> section = streak;
            section.origin.x() = currentX;

            // normalize right overhang
            section.origin.x() -= width;
            section.endX -= width;
            normalizeStreak<TOPOLOGY>(section, target, dimensions);
        }
    }
    
    template<typename TOPOLOGY>
    void normalizeStreak(
        const Streak<DIM>& streak, 
        NewRegion *target, 
        const Coord<DIM>& dimensions) const
    {
        Streak<DIM> ret;
        ret.origin = TOPOLOGY::normalize(streak.origin, dimensions);
        ret.endX = ret.origin.x() + streak.length();

        // it's bad to use a magic value to check for out of bounds
        // accesses, but throwing exceptions would be slower
        if (ret.origin != Coord<DIM>::diagonal(-1)) {
            (*target) << ret;
        }
    }
};

bool PairCompareFirst(const std::pair<int, int>& a, const std::pair<int, int>& b)
{
    return a.first < b.first;
}

template<int DIM>
class NewRegionInsertHelper : public NewRegionCommonHelper
{
public:
    typedef NewRegion<1>::IntPair IntPair;
    typedef NewRegion<1>::VecType VecType;

    template<int MY_DIM>
    inline void operator()(NewRegion<MY_DIM> *region, const Streak<MY_DIM>& s)
    {
        VecType& indices = region->indices[DIM];
        (*this)(region, s, 0, indices.size());
    }

    template<int MY_DIM>
    int operator()(NewRegion<MY_DIM> *region, const Streak<MY_DIM>& s, const int& start, const int& end)
    {
        int c = s.origin[DIM];
        VecType& indices = region->indices[DIM];

        VecType::iterator i = 
            std::upper_bound(
                indices.begin() + start, 
                indices.begin() + end, 
                IntPair(c, 0), 
                PairCompareFirst);

        int nextLevelStart = 0;
        int nextLevelEnd = 0;

        int startOffset = start;
        if (i != (indices.begin() + start)) {
            VecType::iterator entry = i;
            --entry;

            // short-cut: no need to insert if index already present
            if (entry->first == c) {
                nextLevelStart = entry->second;
                nextLevelEnd = region->indices[DIM - 1].size();
                if (i != indices.end()) {
                    nextLevelEnd = i->second;
                }

                int inserts = NewRegionInsertHelper<DIM - 1>()(
                    region, 
                    s, 
                    nextLevelStart,
                    nextLevelEnd);
                incRemainder(i, indices.end(), inserts);
                return 0;
            }
        } 

        if (i != indices.end()) {
            nextLevelStart = i->second;
        } else {
            nextLevelStart = region->indices[DIM - 1].size();
        }
        
        nextLevelEnd = nextLevelStart;
        
        VecType::iterator followingEntries;

        if (i == indices.end()) {
            indices << IntPair(c, nextLevelStart);
            followingEntries = indices.end();
        } else {
            followingEntries = indices.insert(i, IntPair(c, nextLevelStart));
            ++followingEntries;
        }

        int inserts = NewRegionInsertHelper<DIM - 1>()(region, s, nextLevelStart, nextLevelEnd);
        incRemainder(followingEntries, indices.end(), inserts);
        
        return 1;
    }
};

template<>
class NewRegionInsertHelper<0>
{
    friend class NewRegionTest;
public:
    typedef NewRegion<1>::IntPair IntPair;
    typedef NewRegion<1>::VecType VecType;

    template<int MY_DIM>
    inline int operator()(NewRegion<MY_DIM> *region, const Streak<MY_DIM>& s, const int& start, int end)
    {
        IntPair curStreak(s.origin.x(), s.endX);
        VecType& indices = region->indices[0];

        VecType::iterator cursor = 
            std::upper_bound(indices.begin() + start, indices.begin() + end, 
                             curStreak, PairCompareFirst);
        // This will yield the streak AFTER the current origin
        // c. We can't really use lower_bound() as this doesn't
        // replace the < operator by >= but rather by <=, which is
        // IMO really sick...
        if (cursor != (indices.begin() + start)) {
            // ...so we revert to landing one past the streak we're
            // searching and moving back afterwards:
            cursor--;
        }

        int inserts = 1;

        while ((cursor != (indices.begin() + end)) &&
               (curStreak.second >= cursor->first)) {
            if (intersectOrTouch(*cursor, curStreak)) {
                curStreak = fuse(*cursor, curStreak);
                cursor = indices.erase(cursor);
                --end;
                --inserts;
            } else {
                cursor++;
            }
                
            if ((cursor == (indices.begin() + end)) || 
                (!intersectOrTouch(*cursor, curStreak))) {
                break;
            }
        }
        
        indices.insert(cursor, curStreak);
        return inserts;
    }

private:
    inline bool intersectOrTouch(const IntPair& a, const IntPair& b) const
    {
        return 
            ((a.first <= b.first && b.first <= a.second) || 
             (b.first <= a.first && a.first <= b.second));
    }
    
    inline IntPair fuse(const IntPair& a, const IntPair& b) const
    {
        return IntPair(std::min(a.first, b.first),
                       std::max(a.second, b.second));
    }
};

template<int DIM>
class NewRegionRemoveHelper : public NewRegionCommonHelper
{
public:
    typedef NewRegion<1>::IntPair IntPair;
    typedef NewRegion<1>::VecType VecType;

    template<int MY_DIM>
    inline void operator()(NewRegion<MY_DIM> *region, const Streak<MY_DIM>& s)
    {
        VecType& indices = region->indices[DIM];
        (*this)(region, s, 0, indices.size());
    }

    /**
     * tries to remove a streak from the set. Returns the number of
     * inserted streaks (may be negative).
     */
    template<int MY_DIM>
    int operator()(NewRegion<MY_DIM> *region, const Streak<MY_DIM>& s, const int& start, const int& end)
    {
        int c = s.origin[DIM];
        VecType& indices = region->indices[DIM];

        VecType::iterator i = 
            std::upper_bound(
                indices.begin() + start, 
                indices.begin() + end, 
                IntPair(c, 0), 
                PairCompareFirst);

        // key is not present, so no need to remove it
        if (i == (indices.begin() + start)) {
            return 0;
        }

        VecType::iterator entry = i;
        --entry;

        // ditto
        if (entry->first != c) {
            return 0;
        }

        int nextLevelStart = entry->second;
        int nextLevelEnd = region->indices[DIM - 1].size();
        if (i != indices.end()) {
            nextLevelEnd = i->second;
        }

        int inserts = NewRegionRemoveHelper<DIM - 1>()(
            region,
            s,
            nextLevelStart,
            nextLevelEnd);

        int myInserts = 0;

        // current entry needs to be removed if no childs are left
        if ((nextLevelStart - nextLevelEnd) == inserts) {
            entry = indices.erase(entry);
            myInserts = -1;
        } else {
            ++entry;
        }

        incRemainder(entry, indices.end(), inserts);
        return myInserts;
    }
};

template<>
class NewRegionRemoveHelper<0>
{
    friend class NewRegionTest;
public:
    typedef NewRegion<1>::IntPair IntPair;
    typedef NewRegion<1>::VecType VecType;

    template<int MY_DIM>
    int operator()(NewRegion<MY_DIM> *region, const Streak<MY_DIM>& s, const int& start, int end)
    {
        int c = s.origin[0];
        VecType& indices = region->indices[0];
        int inserts = 0;

        // This will yield the streak AFTER the current origin
        // c. We can't really use lower_bound() as this doesn't
        // replace the < operator by >= but rather by <=, which is
        // IMO really sick...
        VecType::iterator cursor = 
            std::upper_bound(
                indices.begin() + start, 
                indices.begin() + end, 
                IntPair(c, 0), 
                PairCompareFirst);
        if (cursor != (indices.begin() + start)) {
            // ...so we resort to landing one past the streak we're
            // searching and moving back afterwards:
            --cursor;
        }

        IntPair curStreak(s.origin.x(), s.endX);

        while (cursor != (indices.begin() + end)) {
            if (intersect(curStreak, *cursor)) {
                VecType newStreaks(substract(*cursor, curStreak));
                cursor = indices.erase(cursor);
                int delta = newStreaks.size() - 1;
                end += delta;
                inserts += delta;

                for (VecType::iterator i = newStreaks.begin(); i != newStreaks.end(); ++i) {
                    cursor = indices.insert(cursor, *i);
                    ++cursor;
                }
            } else {
                ++cursor;
            }

            if (cursor == (indices.begin() + end) || !intersect(*cursor, curStreak)) {
                break;
            }
        }

        return inserts;
    }

private:
    inline bool intersect(const IntPair& a, const IntPair& b) const
    {
        return 
            ((a.first <= b.first && b.first < a.second) || 
             (b.first <= a.first && a.first < b.second));
    }
    
    inline VecType substract(const IntPair& base, const IntPair& minuend) const
    {
        if (!intersect(base, minuend)) {
            return SuperVector<IntPair>(1, base);
        }

        SuperVector<IntPair> ret;
        IntPair s1(base.first, minuend.first);
        IntPair s2(minuend.second, base.second);

        if (s1.second > s1.first) {
            ret.push_back(s1);
        }
        if (s2.second > s2.first) {
            ret.push_back(s2);
        }
        return ret;
    }
};

}

template<typename _CharT, typename _Traits, int _Dim>
std::basic_ostream<_CharT, _Traits>&
operator<<(std::basic_ostream<_CharT, _Traits>& __os,
           const LibGeoDecomp::NewRegion<_Dim>& region)
{
    __os << region.toString();
    return __os;
}


#endif
