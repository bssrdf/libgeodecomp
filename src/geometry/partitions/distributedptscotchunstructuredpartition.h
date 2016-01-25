
#ifndef LIBGEODECOMP_GEOMETRY_PARTITIONS_DISTRIBUTEDPTSCOTCHUNSTRUCTUREDPARTITION_H
#define LIBGEODECOMP_GEOMETRY_PARTITIONS_DISTRIBUTEDPTSCOTCHUNSTRUCTUREDPARTITION_H

#include <libgeodecomp/config.h>
#include <libgeodecomp/geometry/partitions/partition.h>
#include <libgeodecomp/geometry/adjacency.h>

#ifdef LIBGEODECOMP_WITH_CPP14
#ifdef LIBGEODECOMP_WITH_SCOTCH
#ifdef LIBGEODECOMP_WITH_MPI

#include <mpi.h>
#include <ptscotch.h>
#include <chrono>

#ifdef SCOTCH_PTHREAD
#error can only use ptscotch if compiled without SCOTCH_PTHREAD
#endif // SCOTCH_PTHREAD


namespace LibGeoDecomp {

inline void prettyPrint(
            std::ostream& os,
            const std::vector<Region<1>> regions,
            const Coord<2> dimensions,
            const Coord<2>& resolution = Coord<2>(16, 16))
{
    for (int y = 0; y < resolution.y(); ++y) {
        for (unsigned int i = 0; i < regions.size(); ++i) {
            const Region<1>& region = regions[i];

            for (int x = 0; x < resolution.x(); ++x) {
                int cx = ((float)x / (float)resolution.x()) * dimensions.x();
                int cy = ((float)y / (float)resolution.y()) * dimensions.y();

                if (region.count(Coord<1>(cy * dimensions.x() + cx)) > 0) {
                    os << '#';
                } else {
                    os << '.';
                }
            }

            os << ' ';
        }
        os << '\n';
    }
}

inline void prettyPrint(
            std::ostream& os,
            const Region<2>& region,
            const Coord<2>& resolution = Coord<2>(50, 50))
{
    Coord<2> from = region.boundingBox().origin;
    Coord<2> dim = region.boundingBox().dimensions;
    for (int y = 0; y < resolution.y(); ++y) {
        for (int x = 0; x < resolution.x(); ++x) {
            int cx = from.x() + ((float)x / (float)resolution.x()) * dim.x();
            int cy = from.x() + ((float)y / (float)resolution.y()) * dim.y();

            if (region.count(Coord<2>(cx, cy)) > 0) {
                os << '#';
            } else {
                os << '.';
            }
        }
        os << '\n';
    }
}

template<int DIM>
class DistributedPTScotchUnstructuredPartition : public Partition<DIM>
{
public:
    using Partition<DIM>::startOffsets;
    using Partition<DIM>::weights;

    DistributedPTScotchUnstructuredPartition(
            const Coord<DIM>& origin,
            const Coord<DIM>& dimensions,
            const long offset,
            const std::vector<std::size_t>& weights,
            const Adjacency& adjacency) :
        Partition<DIM>(offset, weights),
        origin(origin),
        dimensions(dimensions),
        numPartitions(weights.size())
    {
        this->adjacency = adjacency;

        getStartEnd(MPILayer().rank(), &start, &end);
        localCells = end - start;
        buildRegions();
    }

    DistributedPTScotchUnstructuredPartition(
            const Coord<DIM>& origin,
            const Coord<DIM>& dimensions,
            const long offset,
            const std::vector<std::size_t>& weights,
            Adjacency&& adjacency) :
        Partition<DIM>(offset, weights),
        origin(origin),
        dimensions(dimensions),
        numPartitions(weights.size())
    {
        this->adjacency = std::move(adjacency);

        getStartEnd(MPILayer().rank(), &start, &end);
        localCells = end - start;
        buildRegions();
    }

    void getStartEnd(int rank, std::size_t *start, std::size_t *end)
    {
        std::size_t cells = dimensions.prod();
        *start = (float)rank / numPartitions * cells;
        *end = (float)(rank + 1) / numPartitions * cells;
    }

    Region<DIM> getRegion(const std::size_t node) const override
    {
        return regions.at(node);
    }

private:
    Coord<DIM> origin;
    Coord<DIM> dimensions;
    SCOTCH_Num localCells = 0;
    std::size_t start = 0, end = 0;
    std::size_t numPartitions = 0;
    std::vector<Region<DIM> > regions;

    void buildRegions()
    {
        std::vector<SCOTCH_Num> indices(localCells);
        initIndices(indices);

        regions.resize(numPartitions);
        createRegions(indices);
    }

    void initIndices(std::vector<SCOTCH_Num>& indices)
    {
        // create 2D grid
        SCOTCH_Dgraph graph;
        int error = SCOTCH_dgraphInit(&graph, MPILayer().communicator());

        SCOTCH_Num numEdges = 0;

#ifdef USE_MAP_ADJACENCY
        for (auto& p : this->adjacency) {
            numEdges += p.second.size();
        }
#else
        // ok, this is SUPER ugly
        for (auto& p : this->adjacency.getRegion()) {
            numEdges ++;
        }

        std::vector<int> neighbors;
#endif // USE_MAP_ADJACENCY

        SCOTCH_Num *verttabGra = new SCOTCH_Num[localCells + 1];
        SCOTCH_Num *edgetabGra = new SCOTCH_Num[numEdges];

        int currentEdge = 0;
        for (int i = 0; i < localCells; ++i) {
            verttabGra[i] = currentEdge;

#ifdef USE_MAP_ADJACENCY
            for (int other : this->adjacency[start + i]) {
                edgetabGra[currentEdge++] = other;
            }
#else
            this->adjacency.getNeighbors(start + i,& neighbors);
            for (int other : neighbors) {
                assert(currentEdge < numEdges);
                edgetabGra[currentEdge++] = other;
            }

#endif // USE_MAP_ADJACENCY
        }


        numEdges = currentEdge;

        verttabGra[localCells] = currentEdge;

        error = SCOTCH_dgraphBuild(
                &graph,
                0,              // c++ starts counting at 0
                localCells,     // number of local vertices
                localCells,     // number of maximum local vertices to be created
                verttabGra,     // vertex data
                nullptr,        // these are some ghost zone
                nullptr,        // and other hints that are optional, so -> nullptr
                nullptr,        //
                numEdges,       // number of local edges
                numEdges,       // number of maximum local edges to be created
                edgetabGra,     // edge data
                nullptr,        // more optional data/hints
                nullptr);       //
        if (error) {
            LOG(ERROR, "SCOTCH_graphBuild error: " << error);
        }

        error = SCOTCH_dgraphCheck(&graph);
        if (error) {
            LOG(ERROR, "SCOTCH_graphCheck error: " << error);
        }

        SCOTCH_Strat *straptr = SCOTCH_stratAlloc();
        error = SCOTCH_stratInit(straptr);
        if (error) {
            LOG(ERROR, "SCOTCH_stratInit error: " << error);
        }

        error = SCOTCH_dgraphPart(&graph, numPartitions, straptr,& indices[0]);
        if (error) {
            LOG(ERROR, "SCOTCH_graphMap error: " << error);
        }

        SCOTCH_dgraphExit(&graph);
        SCOTCH_stratExit(straptr);
        delete[] verttabGra;
        delete[] edgetabGra;
    }

    void createRegions(const std::vector<SCOTCH_Num>& indices)
    {
        // regions won't be complete after partitioning, fragments of different regions
        // will end up on different nodes and they have to be synchronized.
        // build partial regions
        std::vector<Region<1>> partials(numPartitions);
        for (int i = 0; i < indices.size(); ++i) {
            partials.at(indices[i]) << Coord<1>(start + i);
        }
        regions = partials;

        // send partial regions to all other nodes so they can build the complete regions
        std::size_t numIndices = indices.size();
        for (size_t j = 0; j < numPartitions; ++j) {
            if (j != MPILayer().rank()) { // dont send own regions to self
                // send all partial regions
                for (size_t k = 0; k < numPartitions; ++k)
                {
                    MPILayer().sendRegion(partials.at(k), j);
                }
            } else {
                // regions will be received from all ranks except self
                for (int i = 0; i < numPartitions; ++i) {
                    if (i == j) continue; // we won't receive regions from ourself

                    // receive parts&  build up own regions
                    for (size_t k = 0; k < numPartitions; ++k) {
                        Region<1> received;
                        MPILayer().recvRegion(&received, i);

                        // add the region to ourself
                        regions.at(k) += received;
                    }
                }
            }
        }

#if defined(PTSCOTCH_PARTITION_PRETTY_PRINT_GRID_SIZE)
        // pretty print regions. those should all be the same in the end
        for (int i = 0; i < MPILayer().size(); ++i) {
            if (MPILayer().rank() == i) {
                // pretty printing to stream first so the output won't
                // get messed up by multithreaded output
                std::stringstream ss;
                prettyPrint(
                        ss,
                        regions,
                        Coord<2>(
                            PTSCOTCH_PARTITION_PRETTY_PRINT_GRID_SIZE,
                            PTSCOTCH_PARTITION_PRETTY_PRINT_GRID_SIZE),
                        Coord<2>(16, 10));
                LOG(DBG, "regions of rank " << i << ": ");
                LOG(DBG, ss.str());
                std::cout << ss.str() << std::endl;
            }

            MPILayer().barrier();
        }
#endif // PTSCOTCH_PARTITION_PRETTY_PRINT_GRID_SIZE

    }

};

}

#endif
#endif
#endif

#endif

