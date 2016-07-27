#ifndef LIBGEODECOMP_PARALLELIZATION_HPXDATAFLOWSIMULATOR_H
#define LIBGEODECOMP_PARALLELIZATION_HPXDATAFLOWSIMULATOR_H

#include <libgeodecomp/config.h>
#ifdef LIBGEODECOMP_WITH_HPX

#include <libgeodecomp/geometry/partitions/unstructuredstripingpartition.h>
#include <libgeodecomp/geometry/partitionmanager.h>
#include <libgeodecomp/communication/hpxreceiver.h>
#include <libgeodecomp/parallelization/hierarchicalsimulator.h>
#include <stdexcept>

#include <mutex>
#include <hpx/include/iostreams.hpp>

namespace LibGeoDecomp {

namespace HPXDataFlowSimulatorHelpers {

template<typename MESSAGE>
class Neighborhood
{
public:
    // fixme: move semantics
    inline Neighborhood(
        int targetGlobalNanoStep,
        std::vector<int> messageNeighborIDs,
        std::vector<hpx::shared_future<MESSAGE> > messagesFromNeighbors,
        const std::map<int, hpx::id_type>& remoteIDs) :
        targetGlobalNanoStep(targetGlobalNanoStep),
        messageNeighborIDs(messageNeighborIDs),
        messagesFromNeighbors(hpx::util::unwrapped(messagesFromNeighbors)),
        remoteIDs(remoteIDs)
    {
        sentNeighbors.reserve(messageNeighborIDs.size());
    }

    const MESSAGE& operator[](int index) const
    {
        std::vector<int>::const_iterator i = std::find(messageNeighborIDs.begin(), messageNeighborIDs.end(), index);
        if (i == messageNeighborIDs.end()) {
            throw std::logic_error("ID not found for incoming messages");
        }

        return messagesFromNeighbors[i - messageNeighborIDs.begin()];
    }

    // fixme: move semantics
    void send(int remoteCellID, const MESSAGE& message)
    {
        std::map<int, hpx::id_type>::const_iterator iter = remoteIDs.find(remoteCellID);
        if (iter == remoteIDs.end()) {
            throw std::logic_error("ID not found for outgoing messages");
        }

        sentNeighbors << remoteCellID;

        hpx::apply(
            typename HPXReceiver<MESSAGE>::receiveAction(),
            iter->second,
            targetGlobalNanoStep,
            message);
    }

    void sendEmptyMessagesToUnnotifiedNeighbors()
    {
        for (int neighbor: messageNeighborIDs) {
            if (std::find(sentNeighbors.begin(), sentNeighbors.end(), neighbor) == sentNeighbors.end()) {
                send(neighbor, MESSAGE());
            }
        }
    }

private:
    int targetGlobalNanoStep;
    std::vector<int> messageNeighborIDs;
    std::vector<MESSAGE> messagesFromNeighbors;
    std::map<int, hpx::id_type> remoteIDs;
    std::vector<int> sentNeighbors;
};

// fixme: componentize
template<typename CELL, typename MESSAGE>
class CellComponent
{
public:
    static const unsigned NANO_STEPS = APITraits::SelectNanoSteps<CELL>::VALUE;
    typedef typename APITraits::SelectMessageType<CELL>::Value MessageType;
    typedef DisplacedGrid<CELL, Topologies::Unstructured::Topology> GridType;


    // fixme: move semantics
    explicit CellComponent(
        const std::string& basename = "",
        boost::shared_ptr<GridType> grid = 0,
        int id = -1,
        const std::vector<int> neighbors = std::vector<int>()) :
        basename(basename),
        grid(grid),
        id(id),
    // fixme: move semantics
        neighbors(neighbors)
    {
        for (auto&& neighbor: neighbors) {
            std::string linkName = endpointName(basename, neighbor, id);
            receivers[neighbor] = HPXReceiver<MESSAGE>::make(linkName).get();
        }
    }

    hpx::shared_future<void> setupDataflow(int maxSteps)
    {
        std::vector<hpx::future<void> > remoteIDFutures;
        remoteIDFutures.reserve(neighbors.size());

        hpx::lcos::local::spinlock mutex;

        for (auto i = neighbors.begin(); i != neighbors.end(); ++i) {
            std::string linkName = endpointName(basename, id, *i);

            int neighbor = *i;
            remoteIDFutures << HPXReceiver<MessageType>::find(linkName).then(
                [&mutex, neighbor, this](hpx::shared_future<hpx::id_type> remoteIDFuture)
                {
                    std::lock_guard<hpx::lcos::local::spinlock> l(mutex);
                    remoteIDs[neighbor] = remoteIDFuture.get();
                });
        }

        hpx::when_all(remoteIDFutures).get(); // swallowing exceptions?
        hpx::shared_future<void> lastTimeStepFuture = hpx::make_ready_future();

        // fixme: add steerer/writer interaction
        for (int step = 0; step < maxSteps; ++step) {
            for (int nanoStep = 0; nanoStep < NANO_STEPS; ++nanoStep) {
                int index = 0;
                int globalNanoStep = step * NANO_STEPS + nanoStep;

                std::vector<hpx::shared_future<MessageType> > receiveMessagesFutures;
                receiveMessagesFutures.reserve(neighbors.size());

                for (auto&& neighbor: neighbors) {
                    if ((globalNanoStep) > 0) {
                        receiveMessagesFutures << receivers[neighbor]->get(globalNanoStep);
                    } else {
                        receiveMessagesFutures <<  hpx::make_ready_future(MessageType());
                    }
                }

                auto Operation = boost::bind(
                    &HPXDataFlowSimulatorHelpers::CellComponent<CELL, MessageType>::update,
                    *this, _1, _2, _3, _4, _5);

                hpx::shared_future<void> thisTimeStepFuture = dataflow(
                    hpx::launch::async,
                    Operation,
                    neighbors,
                    receiveMessagesFutures,
                    lastTimeStepFuture,
                    nanoStep,
                    step);

                using std::swap;
                swap(thisTimeStepFuture, lastTimeStepFuture);
            }
        }

        return lastTimeStepFuture;
    }

    // fixme: use move semantics here
    void update(
        std::vector<int> neighbors,
        std::vector<hpx::shared_future<MESSAGE> > inputFutures,
        // Unused, just here to ensure correct ordering of updates per cell:
        const hpx::shared_future<void>& lastTimeStepReady,
        int nanoStep,
        int step)
    {
        // fixme: lastTimeStepReady.get();

        int targetGlobalNanoStep = step * NANO_STEPS + nanoStep + 1;
        Neighborhood<MESSAGE> hood(targetGlobalNanoStep, neighbors, inputFutures, remoteIDs);

        // fixme: hand over event type which includes a list of neighbors (for zach & dgswem for cross checking consistency)
        cell()->update(hood, nanoStep, step);
        hood.sendEmptyMessagesToUnnotifiedNeighbors();
    }

private:
    std::string basename;
    std::vector<int> neighbors;
    boost::shared_ptr<GridType> grid;
    int id;
    std::map<int, std::shared_ptr<HPXReceiver<MESSAGE> > > receivers;
    std::map<int, hpx::id_type> remoteIDs;

    static std::string endpointName(const std::string& basename, int sender, int receiver)
    {
        return "HPXDataflowSimulatorEndPoint_" +
            basename +
            "_" +
            StringOps::itoa(sender) +
            "_to_" +
            StringOps::itoa(receiver);

    }

    CELL *cell()
    {
        return grid->baseAddress();
    }
};

}

/**
 * Experimental Simulator based on (surprise surprise) HPX' dataflow
 * operator. Primary use case (for now) is DGSWEM.
 */
template<typename CELL, typename PARTITION = UnstructuredStripingPartition>
class HPXDataflowSimulator : public HierarchicalSimulator<CELL>
{
public:
    typedef typename APITraits::SelectMessageType<CELL>::Value MessageType;
    typedef HierarchicalSimulator<CELL> ParentType;
    typedef typename DistributedSimulator<CELL>::Topology Topology;
    typedef PartitionManager<Topology> PartitionManagerType;
    using DistributedSimulator<CELL>::NANO_STEPS;
    using DistributedSimulator<CELL>::initializer;
    using HierarchicalSimulator<CELL>::initialWeights;

    /**
     * basename will be added to IDs for use in AGAS lookup, so for
     * each simulation all localities need to use the same basename,
     * but if you intent to run multiple different simulations in a
     * single program, either in parallel or sequentially, you'll need
     * to use a different basename.
     */
    inline HPXDataflowSimulator(
        Initializer<CELL> *initializer,
        const std::string& basename,
        int loadBalancingPeriod = 10000,
        bool enableFineGrainedParallelism = true) :
        ParentType(
            initializer,
            loadBalancingPeriod * NANO_STEPS,
            enableFineGrainedParallelism),
        basename(basename)
    {}

    void step()
    {
        throw std::logic_error("HPXDataflowSimulator::step() not implemented");
    }

    long currentNanoStep() const
    {
        throw std::logic_error("HPXDataflowSimulator::currentNanoStep() not implemented");
        return 0;
    }

    void balanceLoad()
    {
        throw std::logic_error("HPXDataflowSimulator::balanceLoad() not implemented");
    }
    void run()
    {
        using hpx::dataflow;
        using hpx::util::unwrapped;

        Region<1> localRegion;
        CoordBox<1> box = initializer->gridBox();
        std::size_t rank = hpx::get_locality_id();
        std::size_t numLocalities = hpx::get_num_localities().get();

        std::vector<double> rankSpeeds(numLocalities, 1.0);
        std::vector<std::size_t> weights = initialWeights(
            box.dimensions.prod(),
            rankSpeeds);

        Region<1> globalRegion;
        globalRegion << box;

        boost::shared_ptr<PARTITION> partition(
            new PARTITION(
                box.origin,
                box.dimensions,
                0,
                weights,
                initializer->getAdjacency(globalRegion)));

        PartitionManager<Topology> partitionManager;
        partitionManager.resetRegions(
            initializer,
            box,
            partition,
            rank,
            1);

        localRegion = partitionManager.ownRegion();
        boost::shared_ptr<Adjacency> adjacency = initializer->getAdjacency(localRegion);


        // fixme: instantiate components in agas and only hold ids of those
        typedef HPXDataFlowSimulatorHelpers::CellComponent<CELL, MessageType> ComponentType;
        typedef typename ComponentType::GridType GridType;

        std::map<int, ComponentType> components;
        std::vector<int> neighbors;

        for (Region<1>::Iterator i = localRegion.begin(); i != localRegion.end(); ++i) {
            int id = i->x();
            CoordBox<1> singleCellBox(Coord<1>(id), Coord<1>(1));
            boost::shared_ptr<GridType> grid(new GridType(singleCellBox));
            initializer->grid(&*grid);

            neighbors.clear();
            adjacency->getNeighbors(i->x(), &neighbors);
            HPXDataFlowSimulatorHelpers::CellComponent<CELL, MessageType> component(
                basename,
                grid,
                i->x(),
                neighbors);
            components[i->x()] = component;
        }

        // HPX Reset counters:
        hpx::reset_active_counters();

        typedef hpx::shared_future<void> UpdateResultFuture;
        typedef std::vector<UpdateResultFuture> TimeStepFutures;
        TimeStepFutures lastTimeStepFutures;
        lastTimeStepFutures.reserve(localRegion.size());
        int maxTimeSteps = initializer->maxSteps();

        for (Region<1>::Iterator i = localRegion.begin(); i != localRegion.end(); ++i) {
            lastTimeStepFutures << components[i->x()].setupDataflow(maxTimeSteps);
        }

        hpx::when_all(lastTimeStepFutures).get();
    }

    std::vector<Chronometer> gatherStatistics()
    {
        // fixme
        return std::vector<Chronometer>();
    }

private:
    std::string basename;

};

}

#endif

#endif
